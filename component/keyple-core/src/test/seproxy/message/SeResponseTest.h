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

#include "SelectionStatus.h"
#include "SeResponse.h"
#include "ApduResponseTest.h"

#include <stdexcept>
#include <memory>


namespace keyple {
namespace core {
namespace seproxy {
namespace message {

class SeResponseTest : public std::enable_shared_from_this<SeResponseTest> {
public:
    void
    constructorSuccessfullResponseMatch();

    void
    constructorSuccessfullResponseNoMatch();

    void constructorATRNull();

    void constructorFCINull();

    void constructorFCIAndATRNull();

    void testEquals();

    void testThisEquals();

    void testNotEquals();

    void testNotEqualsNull();

    void hashcode();

    void hashcodeNull();

    /*
     * HELPERS
     */

    static std::shared_ptr<SeResponse>
    getASeResponse();
};

}
}
}
}

