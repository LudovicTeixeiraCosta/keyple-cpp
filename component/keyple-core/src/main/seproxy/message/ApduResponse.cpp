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

#include "ApduResponse.h"
#include "ByteArrayUtil.h"

/* Common */
#include "Arrays.h"
#include "stringhelper.h"

namespace keyple {
namespace core {
namespace seproxy {
namespace message {

using namespace keyple::core::util;

ApduResponse::ApduResponse(
  const std::vector<uint8_t>& buffer,
  const std::shared_ptr<std::set<int>> successfulStatusCodes)
: bytes(buffer)
{
    if (buffer.empty()) {
        logger->debug("empty response\n");
        this->successful = false;
    } else {
        if (buffer.size() < 2) {
            throw std::invalid_argument("Bad buffer (length < 2): " +
                                        StringHelper::to_string(buffer.size()));
        }

        int statusCode = ((buffer[buffer.size() - 2] & 0x000000FF) << 8) +
                         (buffer[buffer.size() - 1] & 0x000000FF);

        if (successfulStatusCodes != nullptr) {
            this->successful = statusCode == 0x9000 ||
                               (successfulStatusCodes->find(statusCode) !=
                                successfulStatusCodes->end());
        } else {
            this->successful = statusCode == 0x9000;
        }
    }
}

bool ApduResponse::isSuccessful() const
{
    return successful;
}

int ApduResponse::getStatusCode() const
{
    if (bytes.size() < 2) {
        logger->debug("bad response length (%)\n", bytes.size());
        return 0;
    }

    int code = ((bytes[bytes.size() - 2] & 0x000000FF) << 8) +
               (bytes[bytes.size() - 1] & 0x000000FF);

    return code;
}

const std::vector<uint8_t>& ApduResponse::getBytes() const
{
    logger->debug("getBytes - 'bytes' size is %\n", this->bytes.size());

    return this->bytes;
}

std::vector<uint8_t> ApduResponse::getDataOut() const
{
    logger->debug("getDataOut - byte size is %\n", this->bytes.size());

    if (this->bytes.size() < 2)
        return std::vector<uint8_t>();

    return Arrays::copyOfRange(this->bytes, 0, this->bytes.size() - 2);
}

bool ApduResponse::equals(std::shared_ptr<void> o)
{
    if (o == shared_from_this()) {
        return true;
    }

    if (!(std::static_pointer_cast<ApduResponse>(o) != nullptr)) {
        return false;
    }

    std::shared_ptr<ApduResponse> resp =
        std::static_pointer_cast<ApduResponse>(o);

    return Arrays::equals(resp->getBytes(), this->bytes) &&
           resp->isSuccessful() == this->successful;
}

int ApduResponse::hashCode()
{
    int hash = 17;
    hash     = 19 * hash + (this->successful ? 0 : 1);
    hash     = 31 * hash + (bytes.empty() ? 0 : Arrays::hashCode(bytes));
    return hash;
}

void ApduResponse::finalize()
{
}

std::ostream& operator<<(std::ostream& os, const ApduResponse& r)
{
    const std::string status = r.successful ? "SUCCESS" : "FAILURE";

	os << "R-APDU: {"
	   << "STATUS = " << status << ", "
	   << "BYTES (" << r.bytes.size() << ") = " << r.bytes
	   << "}";

	return os;
}

std::ostream& operator<<(std::ostream& os,
	                     const std::shared_ptr<ApduResponse>& r)
{
    if (r)
		os << *(r.get());
    else
		os << "R-APDU: null";

    return os;
}

std::ostream& operator<<(
	std::ostream& os, const std::vector<std::shared_ptr<ApduResponse>>& v)
{
    os << "APDURESPONSES: {";
	for (int i = 0; i < (int)v.size(); i++) {
		os << *(v[i].get());
		if (i != (int)v.size() - 1)
			os << ", ";
    }
	os << "}";

	return os;
}

}
}
}
}
