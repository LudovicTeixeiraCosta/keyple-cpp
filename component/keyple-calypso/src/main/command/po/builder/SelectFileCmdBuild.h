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

#pragma once

#include <memory>
#include <vector>

#include "AbstractPoCommandBuilder.h"
#include "CalypsoPoCommands.h"
#include "PoClass.h"
#include "PoSendableInSession.h"
#include "SelectFileRespPars.h"

#include "exceptionhelper.h"

namespace keyple {
namespace calypso {
namespace command {
namespace po {
namespace builder {

using namespace keyple::calypso::command;
using namespace keyple::calypso::command::po;
using namespace keyple::calypso::command::po::parser;
using namespace keyple::core::seproxy::message;

/**
 * This class provides the dedicated constructor to build the Select File APDU
 * commands.
 *
 */
class KEYPLECALYPSO_API SelectFileCmdBuild final
: public AbstractPoCommandBuilder<SelectFileRespPars>,
  public PoSendableInSession {
public:
    /**
     *
     */
    enum class SelectControl { FIRST, NEXT, CURRENT_DF };

    /**
     * Instantiates a new SelectFileCmdBuild to select the first, next or
     * current file in the current DF.
     *
     * @param poClass indicates which CLA byte should be used for the Apdu
     * @param selectControl the selection mode control: FIRST, NEXT or CURRENT
     */
    SelectFileCmdBuild(PoClass poClass, SelectControl selectControl);

    /**
     * Instantiates a new SelectFileCmdBuild to select the first, next or
     * current file in the current DF.
     *
     * @param poClass indicates which CLA byte should be used for the Apdu
     * @param selectionPath the file identifier path
     */
    SelectFileCmdBuild(PoClass poClass,
                       const std::vector<uint8_t>& selectionPath);

    /**
     *
     */
    std::shared_ptr<SelectFileRespPars>
    createResponseParser(std::shared_ptr<ApduResponse> apduResponse) override;

protected:
    /**
     *
     */
    std::shared_ptr<SelectFileCmdBuild> shared_from_this()
    {
        return std::static_pointer_cast<SelectFileCmdBuild>(
            AbstractPoCommandBuilder<SelectFileRespPars>::shared_from_this());
    }

private:
    /**
     *
     */
    CalypsoPoCommands& command = CalypsoPoCommands::SELECT_FILE;
};

/**
 *
 */
std::ostream& operator<<(std::ostream& os,
                         const SelectFileCmdBuild::SelectControl& sc);

}
}
}
}
}
