/******************************************************************************
 * Copyright (c) 2018 Calypso Networks Association                            *
 * https://www.calypsonet-asso.org/                                           *
 *                                                                            *
 * See the NOTICE file(s) distributed with this work for additional           *
 * information regarding copyright ownership.                                 *
 *                                                                            *
 * This program and the accompanying materials are made available under the   *
 * terms of the Eclipse Public License 2.0 which is available at              *
 * http://www.eclipse.org/legal/epl-2.0                                       *
 *                                                                            *
 * SPDX-License-Identifier: EPL-2.0                                           *
 ******************************************************************************/

/* Common */
#include "InterruptedException.h"
#include "Pattern.h"
#include "System.h"
#include "Thread.h"

/* Core */
#include "ByteArrayUtil.h"
#include "KeypleAllocationReaderException.h"
#include "KeypleIOReaderException.h"
#include "KeyplePluginNotFoundException.h"
#include "MatchingSelection.h"
#include "ReaderPoolPlugin.h"
#include "SeCommonProtocols.h"
#include "SeProxyService.h"
#include "SeReader.h"
#include "SeSelection.h"

/* Calypso */
#include "CalypsoNoSamResourceAvailableException.h"
#include "SamResourceManager.h"
#include "SamSelectionRequest.h"
#include "SamSelector.h"
#include "SamRevision.h"

namespace keyple {
namespace calypso {
namespace transaction {

using namespace keyple::common;
using namespace keyple::core::selection;
using namespace keyple::core::seproxy;
using namespace keyple::core::seproxy::event;
using namespace keyple::core::seproxy::exception;
using namespace keyple::core::seproxy::protocol;
using namespace keyple::core::util;

using ReaderObserver = ObservableReader::ReaderObserver;
using PluginObserver = ObservablePlugin::PluginObserver;

const int SamResourceManager::MAX_BLOCKING_TIME = 1000; // 10 sec

SamResourceManager::SamResourceManager(
    std::shared_ptr<ReaderPlugin> samReaderPlugin,
    const std::string& samReaderFilter, int maxBlockingTime)
: maxBlockingTime(maxBlockingTime), samReaderPlugin(samReaderPlugin)
{
    if (std::dynamic_pointer_cast<ReaderPoolPlugin>(samReaderPlugin)) {
        logger->info("Create SAM resource manager from reader pool plugin: "
                     "%s\n",
                     samReaderPlugin->getName().c_str());

        /* HSM reader plugin type */
        dynamicAllocationPlugin = true;
    } else {
        logger->info("Create SAM resource manager from reader plugin: %s\n",
                     samReaderPlugin->getName().c_str());

        /* Local readers plugin type */
        dynamicAllocationPlugin = false;

        if (std::dynamic_pointer_cast<ObservablePlugin>(samReaderPlugin)) {
            /* Add an observer to monitor reader and SAM insertions */
            std::shared_ptr<ReaderObserver> readerObserver =
                std::make_shared<ReaderObserver>(this);

            std::shared_ptr<PluginObserver> pluginObserver =
                std::make_shared<PluginObserver>(this, readerObserver,
                                                 samReaderFilter);

            logger->info("Add observer PLUGINNAME = %s\n",
                         samReaderPlugin->getName().c_str());

            std::dynamic_pointer_cast<ObservablePlugin>(samReaderPlugin)
                ->addObserver(pluginObserver);
        } else {
            /*
             * The plugin isn't observable, just add resources from the current
             * readers if any
             */
            logger->info("PLUGINNAME = %s isn't observable. Add available "
                         "readers\n",
                         samReaderPlugin->getName().c_str());

            std::set<std::shared_ptr<SeReader>> samReaders =
                samReaderPlugin->getReaders();

            for (std::shared_ptr<SeReader> samReader : samReaders) {
                std::string readerName = samReader->getName();
                Pattern* p             = Pattern::compile(samReaderFilter);
                if (p->matcher(readerName)->matches()) {
                    logger->debug("Add reader: %s\n", readerName.c_str());
                    localSamResources.push_back(createSamResource(samReader));
                } else {
                    logger->debug("Reader not matching: %s\n",
                                  readerName.c_str());
                }
            }
        }
    }
}

SamResourceManager::SamResourceManager(
  std::shared_ptr<ReaderPlugin> samReaderPlugin,
  const std::string& samReaderFilter)
: SamResourceManager(samReaderPlugin, samReaderFilter, MAX_BLOCKING_TIME)
{
}

std::unique_ptr<SamResource>
SamResourceManager::createSamResource(std::shared_ptr<SeReader> samReader)
{
    logger->trace("Create SAM resource from reader NAME = %s\n",
                  samReader->getName().c_str());

    samReader->addSeProtocolSetting(SeCommonProtocols::PROTOCOL_ISO7816_3,
                                    ".*");

    SamIdentifier samIdentifier(SamRevision::AUTO, "", "");
    std::shared_ptr<SamSelector> samSelector =
        std::make_shared<SamSelector>(samIdentifier, "SAM");

    /* Prepare selector, ignore MatchingSe here */
    SeSelection samSelection;
    samSelection.prepareSelection(
        std::make_shared<SamSelectionRequest>(samSelector));

    std::shared_ptr<SelectionsResult> selectionsResult =
        samSelection.processExplicitSelection(samReader);

    if (!selectionsResult->hasActiveSelection()) {
        throw IllegalStateException("Unable to open a logical channel for SAM");
    }

    std::shared_ptr<CalypsoSam> calypsoSam =
        std::dynamic_pointer_cast<CalypsoSam>(
            selectionsResult->getActiveSelection()->getMatchingSe());

    return std::unique_ptr<SamResource>(new SamResource(samReader, calypsoSam));
}

std::unique_ptr<SamResource>
SamResourceManager::allocateSamResource(const AllocationMode allocationMode,
                                        const SamIdentifier& samIdentifier)
{
    unsigned long long maxBlockingDate =
        System::currentTimeMillis() + maxBlockingTime;
    bool noSamResourceLogged = false;

    logger->debug("Allocating SAM reader channel...\n");

    while (true) {
        if (dynamicAllocationPlugin) {
            // virtually infinite number of readers
            std::shared_ptr<SeReader> samReader = nullptr;
            try {
                    samReader =
                        (std::dynamic_pointer_cast<ReaderPoolPlugin>(
                            samReaderPlugin))
                            ->allocateReader(samIdentifier.getGroupReference());
                } catch (const KeypleAllocationReaderException& e) {
                    throw CalypsoNoSamResourceAvailableException(
                              e.getMessage(), e);
                }

            if (samReader != nullptr) {
                std::unique_ptr<SamResource> samResource =
                    createSamResource(samReader);
                logger->debug("Allocation succeeded. SAM resource created\n");
                return samResource;
            }
        } else {
            std::unique_lock<std::mutex> lock(this->mtx);
            for (std::unique_ptr<SamResource>& samResource :
                 localSamResources) {
                if (samResource->isSamResourceFree()) {
                    if (samResource->isSamMatching(samIdentifier)) {
                        samResource->setSamResourceStatus(
                            SamResource::SamResourceStatus::BUSY);
                        logger->debug("Allocation succeeded. SAM resource "
                                      "created.");
                        std::unique_lock<std::mutex> unlock(this->mtx);
                        return std::move(samResource);
                    }
                }
            }
            std::unique_lock<std::mutex> unlock(this->mtx);
        }

        /*
         * Loop until MAX_BLOCKING_TIME in blocking mode, only once in
         * non-blocking mode
         */
        if (allocationMode == AllocationMode::NON_BLOCKING) {
            logger->trace("No SAM resources available at the moment\n");
            break;
        } else {
            if (!noSamResourceLogged) {
                /* log once the first time */
                logger->trace("No SAM resources available at the moment\n");
                noSamResourceLogged = true;
            }

            try {
                Thread::sleep(10);
            } catch (InterruptedException& e) {
                (void)e;
                //Thread::currentThread().interrupt(); // set interrupt flag
                logger->error("Interrupt exception in Thread::sleep\n");
            }

            if (System::currentTimeMillis() >= maxBlockingDate) {
                throw CalypsoNoSamResourceAvailableException(
                          "The allocation process has timed out.");
            }
        }
    }

    throw CalypsoNoSamResourceAvailableException(
              "The allocation process has failed.");
}

void SamResourceManager::freeSamResource(
    std::unique_ptr<SamResource> samResource)
{
    if (dynamicAllocationPlugin) {
        /* Virtually infinite number of readers */
        logger->debug("Freeing HSM SAM resource\n");
        (std::dynamic_pointer_cast<ReaderPoolPlugin>(samReaderPlugin))
            ->releaseReader(samResource->getSeReader());
    } else {
        std::unique_lock<std::mutex> lock(this->mtx);
        logger->debug("Freeing local SAM resource\n");
        samResource->setSamResourceStatus(SamResource::SamResourceStatus::FREE);
        std::unique_lock<std::mutex> unlock(this->mtx);
    }
}

void SamResourceManager::removeResource(std::shared_ptr<SeReader> samReader)
{
    std::list<std::unique_ptr<SamResource>>::iterator samResource =
        localSamResources.begin();

    while (samResource != localSamResources.end()) {
        if ((*samResource)->getSeReader() == samReader) {
            logger->info(
                "Freed SAM resource: READER = %s, SAM_REVISION = %s,"
                " SAM_SERIAL_NUMBER = %s",
                samReader->getName().c_str(),
                (*samResource)->getMatchingSe()->getSamRevision().getName()
                    .c_str(),
                ByteArrayUtil::toHex(
                    (*samResource)->getMatchingSe()->getSerialNumber())
                        .c_str());
            localSamResources.remove(*samResource);
        }
    }
}

SamResourceManager::PluginObserver::PluginObserver(
    SamResourceManager* parent, std::shared_ptr<ReaderObserver> readerObserver,
    const std::string samReaderFilter)
: readerObserver(readerObserver), samReaderFilter(samReaderFilter), p(nullptr),
  logger(parent->logger), parent(parent)
{
}

void SamResourceManager::PluginObserver::update(
    std::shared_ptr<PluginEvent> event)
{
    for (auto& readerName : event->getReaderNames()) {
        std::shared_ptr<SeReader> samReader = nullptr;

        logger->info("PluginEvent: PLUGINNAME = %s, READERNAME = %s, "
                     "EVENTTYPE = %s\n",
                     event->getPluginName().c_str(), readerName.c_str(),
                     event->getEventType().getName().c_str());

        /* We retrieve the reader object from its name. */
        try {
            samReader = SeProxyService::getInstance()
                            .getPlugin(event->getPluginName())
                            ->getReader(readerName);

        } catch (KeyplePluginNotFoundException& e) {
            (void)e;
            logger->error("Plugin not found %s\n",
                          event->getPluginName().c_str());
        } catch (KeypleReaderNotFoundException& e) {
            (void)e;
            logger->error("Reader not found %s\n", readerName.c_str());
        }

        if (event->getEventType() == PluginEvent::EventType::READER_CONNECTED) {
            logger->info("New reader! READERNAME = %s\n",
                         samReader->getName().c_str());

            /*
            * We are informed here of a connection of a reader.
            *
            * We add an observer to this reader if possible.
            */
            p = Pattern::compile(samReaderFilter);
            if (p->matcher(readerName)->matches()) {
                /* Enable logging */
                try {
                    /* contactless SE works with T0 protocol */
                    samReader->setParameter("protocol", "T0");

                    /* Shared mode */
                    samReader->setParameter("mode", "shared");
                } catch (KeypleBaseException& e) {
                    logger->error("Wrong parameter. %s\n",
                                  e.getMessage().c_str());
                }

                std::shared_ptr<ObservableReader> observable =
                    std::dynamic_pointer_cast<ObservableReader>(samReader);

                if (observable && readerObserver) {
                    logger->info("Add observer READERNAME = %s\n",
                                 samReader->getName().c_str());
                    observable->addObserver(readerObserver);
                } else {
                    logger->info("No observer to add READERNAME = %s\n",
                                 samReader->getName().c_str());
                    try {
                        if (samReader->isSePresent()) {
                            logger->debug("Create SAM resource: %s\n",
                                          readerName.c_str());
                            std::unique_lock<std::mutex> lock(parent->mtx);
                            parent->localSamResources.push_back(
                                parent->createSamResource(samReader));
                            std::unique_lock<std::mutex> unlock(parent->mtx);
                        }
                    } catch (KeypleIOReaderException& e) {
                        logger->error("Error in reader. %s\n",
                                      e.getMessage().c_str());
                    } catch (KeypleReaderException& e) {
                        logger->error("Error in reader. %s\n",
                                      e.getMessage().c_str());
                    }
                }
            } else {
                logger->debug("Reader not matching: %s\n", readerName.c_str());
            }
        } else if (event->getEventType() ==
                   PluginEvent::EventType::READER_DISCONNECTED) {
            /*
            * We are informed here of a disconnection of a reader.
            *
            * The reader object still exists but will be removed from the
            * reader list right after. Thus, we can properly remove the
            * observer attached to this reader before the list update.
            */
            p = Pattern::compile(samReaderFilter);
            if (p->matcher(readerName)->matches()) {

                logger->info("Reader removed. READERNAME = %s\n",
                             readerName.c_str());

                std::shared_ptr<ObservableReader> observable =
                    std::dynamic_pointer_cast<ObservableReader>(samReader);

                if (observable) {
                    if (readerObserver) {
                        logger->info("Remove observer READERNAME = %s\n",
                                     readerName.c_str());
                        observable->removeObserver(readerObserver);
                    } else {
                        parent->removeResource(samReader);
                        logger->info("Unplugged reader READERNAME = %s "
                                     "wasn't observed. Resource removed\n",
                                     readerName.c_str());
                    }
                }
            } else {
                logger->debug("Reader not matching: %s\n", readerName.c_str());
            }
        } else {
            logger->info("Unexpected reader event-> EVENT = %s\n",
                         event->getEventType().getName().c_str());
        }
    }
}

SamResourceManager::ReaderObserver::ReaderObserver(SamResourceManager* parent)
: logger(parent->logger), parent(parent)
{
}

void SamResourceManager::ReaderObserver::update(
    std::shared_ptr<ReaderEvent> event)
{
    std::shared_ptr<SeReader> samReader = nullptr;

    try {
        samReader = parent->samReaderPlugin->getReader(event->getReaderName());
    } catch (KeypleReaderNotFoundException& e) {
        logger->error("Reader not found. %s\n", e.getMessage().c_str());
    }

    std::unique_lock<std::mutex> lock(parent->mtx);

    if (event->getEventType() == ReaderEvent::EventType::SE_MATCHED ||
        event->getEventType() == ReaderEvent::EventType::SE_INSERTED) {
        std::unique_ptr<SamResource> newSamResource = nullptr;
        try {
            /*
             * although the reader allocation is dynamic, the SAM resource type is
             * STATIC
             */
            newSamResource = parent->createSamResource(samReader);
        } catch (KeypleReaderException& e) {
            logger->error("Reader failure while creating a SamResource from "
                          "%s. %s\n",
                          samReader->getName().c_str(), e.getMessage().c_str());
            std::unique_lock<std::mutex> unlock(parent->mtx);
        }

        /* failures are ignored */
        if (newSamResource) {
            logger->info(
                "Created SAM resource: READER = %s, SAM_REVISION = "
                "%d, SAM_SERIAL_NUMBER = %s",
                event->getReaderName().c_str(),
                newSamResource->getMatchingSe()->getSamRevision().getName()
                    .c_str(),
                ByteArrayUtil::toHex(
                    newSamResource->getMatchingSe()->getSerialNumber())
                        .c_str());
            parent->localSamResources.push_back(std::move(newSamResource));
        }
    } else if (event->getEventType() == ReaderEvent::EventType::SE_REMOVED ||
               event->getEventType() == ReaderEvent::EventType::TIMEOUT_ERROR) {
        parent->removeResource(samReader);
    }

    std::unique_lock<std::mutex> unlock(parent->mtx);
}

}
}
}
