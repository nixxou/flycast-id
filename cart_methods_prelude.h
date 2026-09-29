// What the Cartridge methods taken out of naomi_cart.cpp need - see CMakeLists.txt.
#pragma once
#include <algorithm>
#include <cstring>
#include "types.h"
#include "emulator.h"
#include "hw/naomi/naomi_cart.h"
#include "hw/naomi/naomi_regs.h"
#include "serialize.h"
#include "oslib/i18n.h"
#include "hw/naomi/naomi.h"
#include "hw/naomi/decrypt.h"
using namespace i18n;
