// TV-B-Gone IR codes for European TVs, ported from m5stick-weather-tvbgone
#pragma once
#include <stdint.h>

enum IRProto : uint8_t { P_NEC, P_SAMSUNG, P_SONY12, P_SONY15, P_SONY20, P_RC5, P_RC6, P_PANASONIC, P_JVC };

struct TVCode {
  const char* marca;
  IRProto proto;
  uint64_t code;
  uint8_t  bits;
};

const TVCode codigosEU[] = {
  // ===== SAMSUNG (protocolo Samsung 32-bit) =====
  {"Samsung",         P_SAMSUNG,   0xE0E040BF, 32},
  {"Samsung Off",     P_SAMSUNG,   0xE0E019E6, 32},
  {"Samsung Old",     P_SAMSUNG,   0xB4B4E21D, 32},
  {"Samsung BN",      P_SAMSUNG,   0xE0E0E21D, 32},
  {"Samsung AA59",    P_SAMSUNG,   0xE0E09966, 32},

  // ===== LG (NEC 32-bit) =====
  {"LG",              P_NEC,       0x20DF10EF, 32},
  {"LG Off",          P_NEC,       0x20DF23DC, 32},
  {"LG Old",          P_NEC,       0x20DFA25D, 32},
  {"LG AKB",          P_NEC,       0x20DF08F7, 32},

  // ===== SONY SIRC (12, 15, 20 bit — 3 automatic sends) =====
  {"Sony 12",         P_SONY12,    0xA90,      12},
  {"Sony 15",         P_SONY15,    0x540C,     15},
  {"Sony 20a",        P_SONY20,    0x060A90,   20},
  {"Sony 20b",        P_SONY20,    0x010A90,   20},
  {"Sony Bravia",     P_SONY20,    0x040A90,   20},
  {"Sony KD",         P_SONY12,    0xF50,      12},
  {"Sony RM",         P_SONY15,    0x5C0C,     15},

  // ===== PHILIPS RC5 (both toggle states) =====
  {"Philips RC5 T0",  P_RC5,       0x100C,     13},
  {"Philips RC5 T1",  P_RC5,       0x180C,     13},
  {"Philips a1 T0",   P_RC5,       0x104C,     13},
  {"Philips a1 T1",   P_RC5,       0x184C,     13},

  // ===== PHILIPS RC6 =====
  {"Philips RC6",     P_RC6,       0x100C,     20},
  {"Philips RC6 T",   P_RC6,       0x1800C,    21},
  {"Philips RC6 v2",  P_RC6,       0x1000C,    20},

  // ===== PANASONIC (48-bit) =====
  {"Panasonic",       P_PANASONIC, 0x400401007C7D, 48},
  {"Panasonic Viera", P_PANASONIC, 0x400401003D3C, 48},
  {"Panasonic Old",   P_PANASONIC, 0x400401000102, 48},
  {"Panasonic 4",     P_PANASONIC, 0x400401003C3D, 48},

  // ===== TOSHIBA (NEC) =====
  {"Toshiba",         P_NEC,       0x02FD48B7, 32},
  {"Toshiba Regza",   P_NEC,       0x02FD807F, 32},
  {"Toshiba CT90",    P_NEC,       0x40BF12ED, 32},
  {"Toshiba CT-8",    P_NEC,       0x02FDA857, 32},

  // ===== SHARP (NEC) =====
  {"Sharp",           P_NEC,       0x5EA1F807, 32},
  {"Sharp AQUOS",     P_NEC,       0x5EA1B847, 32},
  {"Sharp Old",       P_NEC,       0x5EA158A7, 32},
  {"Sharp 4",         P_NEC,       0x5EA1D827, 32},

  // ===== HISENSE =====
  {"Hisense",         P_NEC,       0x0000F20D, 32},
  {"Hisense EN2B",    P_NEC,       0x80FF40BF, 32},
  {"Hisense ERF3",    P_NEC,       0x80BF40BF, 32},
  {"Hisense Old",     P_NEC,       0x00FF00FF, 32},

  // ===== TCL =====
  {"TCL",             P_NEC,       0x00FF00FF, 32},
  {"TCL RC802N",      P_NEC,       0x40BF40BF, 32},
  {"TCL RC802V",      P_NEC,       0x80FF40BF, 32},
  {"TCL Thomson",     P_NEC,       0x02FD48B7, 32},

  // ===== GRUNDIG =====
  {"Grundig RC5",     P_RC5,       0x100C,     13},
  {"Grundig NEC",     P_NEC,       0x00FF00FF, 32},
  {"Grundig 3",       P_NEC,       0x807F40BF, 32},

  // ===== VESTEL (OEM: Telefunken, Finlux, Techwood, Orion, etc.) =====
  {"Vestel RC4875",   P_NEC,       0x00FF00FF, 32},
  {"Vestel RC4870",   P_NEC,       0x00FF40BF, 32},
  {"Vestel RC4876",   P_NEC,       0x00FF18E7, 32},
  {"Vestel v2",       P_NEC,       0x807F40BF, 32},
  {"Vestel v3",       P_NEC,       0x00FF807F, 32},

  // ===== THOMSON / SABA / NORDMENDE (grupo Thomson) =====
  {"Thomson",         P_NEC,       0x08F748B7, 32},
  {"Thomson 2",       P_NEC,       0x40BF40BF, 32},
  {"Thomson RC5",     P_RC5,       0x100C,     13},
  {"Saba",            P_NEC,       0x08F748B7, 32},
  {"Nordmende",       P_NEC,       0x08F740BF, 32},

  // ===== LOEWE =====
  {"Loewe RC5",       P_RC5,       0x100C,     13},
  {"Loewe NEC",       P_NEC,       0x30CF708F, 32},
  {"Loewe 3",         P_NEC,       0x30CF40BF, 32},

  // ===== METZ =====
  {"Metz RC5",        P_RC5,       0x100C,     13},
  {"Metz NEC",        P_NEC,       0x00FF38C7, 32},
  {"Metz RM",         P_RC5,       0x180C,     13},

  // ===== HITACHI =====
  {"Hitachi",         P_NEC,       0x0AF5D02F, 32},
  {"Hitachi 2",       P_NEC,       0x0AF548B7, 32},
  {"Hitachi CLE",     P_NEC,       0x0AF5807F, 32},

  // ===== SANYO =====
  {"Sanyo",           P_NEC,       0x1CE348B7, 32},
  {"Sanyo 2",         P_NEC,       0x1CE3807F, 32},

  // ===== JVC (protocolo JVC 16-bit) =====
  {"JVC",             P_JVC,       0xC5E8,     16},
  {"JVC 2",           P_JVC,       0xC5F0,     16},
  {"JVC 3",           P_JVC,       0xC538,     16},

  // ===== DAEWOO =====
  {"Daewoo",          P_NEC,       0x14EB38C7, 32},
  {"Daewoo 2",        P_NEC,       0x14EB08F7, 32},

  // ===== HAIER =====
  {"Haier",           P_NEC,       0x1901E817, 32},
  {"Haier 2",         P_NEC,       0x19010807, 32},

  // ===== XIAOMI / MI TV =====
  {"Xiaomi Mi TV",    P_NEC,       0x44BB40BF, 32},
  {"Xiaomi 2",        P_NEC,       0x44BB807F, 32},

  // ===== SPANISH / BUDGET EU BRANDS =====
  {"Nevir",           P_NEC,       0x807F40BF, 32},
  {"TD Systems",      P_NEC,       0x00FF00FF, 32},
  {"TD Systems 2",    P_NEC,       0x807F40BF, 32},
  {"Infiniton",       P_NEC,       0x00FF00FF, 32},
  {"EAS Electric",    P_NEC,       0x807F40BF, 32},
  {"Kunft",           P_NEC,       0x00FF00FF, 32},
  {"OK.",             P_NEC,       0x00FF00FF, 32},
  {"CHiQ",            P_NEC,       0x40BF40BF, 32},
  {"Cecotec",         P_NEC,       0x00FF00FF, 32},

  // ===== OTHER EU MANUFACTURERS =====
  {"Medion",          P_NEC,       0x00FF00FF, 32},
  {"Dyon",            P_NEC,       0x00FF00FF, 32},
  {"Hyundai TV",      P_NEC,       0x807F40BF, 32},
  {"Finlux",          P_NEC,       0x00FF00FF, 32},
  {"Continental E",   P_NEC,       0x00FF00FF, 32},
  {"Techwood",        P_NEC,       0x00FF00FF, 32},
  {"Orion",           P_NEC,       0x00FF00FF, 32},
  {"Nokia TV",        P_NEC,       0x00FF00FF, 32},

  // ===== RC5 EXTRA ADDRESSES (various European TVs) =====
  {"RC5 Addr2 T0",    P_RC5,       0x108C,     13},
  {"RC5 Addr2 T1",    P_RC5,       0x188C,     13},
  {"RC5 Addr3 T0",    P_RC5,       0x10CC,     13},
  {"RC5 Addr3 T1",    P_RC5,       0x18CC,     13},
  {"RC5 Addr5 T0",    P_RC5,       0x114C,     13},
  {"RC5 Addr5 T1",    P_RC5,       0x194C,     13},
  {"RC5 Addr8 T0",    P_RC5,       0x120C,     13},
  {"RC5 Addr8 T1",    P_RC5,       0x1A0C,     13},

  // ===== GENERIC NEC CODES (catch-all for EU universal remotes) =====
  {"NEC gen 1",       P_NEC,       0x10EF08F7, 32},
  {"NEC gen 2",       P_NEC,       0x04FB40BF, 32},
  {"NEC gen 3",       P_NEC,       0x08F740BF, 32},
  {"NEC gen 4",       P_NEC,       0xA0C8A857, 32},
  {"NEC gen 5",       P_NEC,       0x10EF40BF, 32},
  {"NEC gen 6",       P_NEC,       0xC0C840BF, 32},
};
const int numCodigosEU = sizeof(codigosEU) / sizeof(codigosEU[0]);
