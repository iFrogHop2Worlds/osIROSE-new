#include "cli_acceptreq.h"

namespace RoseCommon {

CliAcceptReq::CliAcceptReq() : CRosePacket(ePacketType::PAKCS_ACCEPT_REQ) {}

CliAcceptReq::CliAcceptReq(uint8_t buffer[MAX_PACKET_SIZE]) : CRosePacket(buffer) {
	if (type() != ePacketType::PAKCS_ACCEPT_REQ)
		throw std::runtime_error("Not the right packet!");
}


void CliAcceptReq::pack() {
}

}