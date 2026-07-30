// wM-Buster ADV — Techem Family Payload Decoders
// Hand-ported proprietary payload parsers for Techem meters (GPL-3.0)
#pragma once

#include "wmbus_decode/interpreter.h"

namespace wmb {

// Register all Techem payload decoders with the interpreter engine
void register_techem_payload_decoders();

} // namespace wmb
