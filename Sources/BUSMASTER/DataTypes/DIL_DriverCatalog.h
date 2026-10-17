/*
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * \file      DIL_DriverCatalog.h
 * \brief     Single source of truth for the DIL driver list
 *
 * This header is the one place that knows the driver IDs, the DIL DLL
 * implementing each driver and the menu label shown in the UI. The driver
 * selection dialogs, the DLL loader in DIL_CAN.cpp / DIL_LIN.cpp and the
 * configuration file round trip all derive from the tables below. To add
 * a driver, add a row to the catalog and a matching enumerator - nothing else.
 */

#pragma once

/**
 * CAN driver IDs. The numeric values are persisted in configuration files
 * and passed across DLL boundaries - never insert, remove or reorder entries.
 */
enum
{
    DRIVER_CAN_STUB = 0,
    DRIVER_CAN_PEAK_USB,
    DRIVER_CAN_ICS_NEOVI,
    DRIVER_CAN_ETAS_BOA,
    DRIVER_CAN_ETAS_ES581,
    DRIVER_CAN_ETAS_ES5814,
    DRIVER_CAN_ETAS_ES5821,
    DRIVER_CAN_VECTOR_XL,
    DRIVER_CAN_KVASER_CAN,
    DRIVER_CAN_MHS,
    DRIVER_CAN_NSI,
    DRIVER_CAN_IXXAT,
    DRIVER_CAN_VSCOM,
    DRIVER_CAN_IVIEW,
    DRIVER_CAN_ISOLAR,
    DIL_TOTAL,          // Its value must be <= MAX_DILS
    DAL_NONE = ~0x0
};

/**
 * LIN driver IDs. The numeric values are persisted in configuration files
 * and passed across DLL boundaries - never insert, remove or reorder entries.
 */
enum
{
    DRIVER_LIN_ISOLAR_EVE_VLIN = 0,
    DRIVER_LIN_VECTOR_XL,
    DRIVER_LIN_ETAS_BOA,
    DRIVER_LIN_PEAK_USB,
    DRIVER_LIN_KVASER,
    DIL_LIN_TOTAL,
    DAL_LIN_NONE = ~0x0
};

/**
 * One selectable driver: its ID, the DIL DLL implementing it and the menu
 * label shown in the UI ('&' marks the keyboard accelerator).
 * A nullptr DLL name means the library is resolved at selection time:
 * the ETAS BOA entries probe the registry for the installed BOA version,
 * and the LIN "Deactivate" entry has no library at all.
 */
struct SDILDriverEntry
{
    unsigned long m_dwDriverID;
    const char*   m_pcDILLib;
    const char*   m_pcMenuLabel;
};

/*
 * CAN driver catalog in menu order: simulation first, all other drivers
 * in alphabetical order.
 */
static const SDILDriverEntry sg_asDriverCatalogCAN[] =
{
    {DRIVER_CAN_STUB,        "CAN_STUB.dll",            "&Simulation"       },
    {DRIVER_CAN_ETAS_BOA,    nullptr,                   "ETAS &BOA"         },
    {DRIVER_CAN_ETAS_ES581,  "CAN_ICS_neoVI.dll",       "ETAS &ES581.3"     },
    {DRIVER_CAN_ETAS_ES5814, nullptr,                   "ETAS ES581.&4"     },
    {DRIVER_CAN_ETAS_ES5821, nullptr,                   "ETAS ES582.&1"     },
    {DRIVER_CAN_ISOLAR,      "CAN_ISOLAR_EVE_VCAN.dll", "ETAS ISO&LAR-EVE"  },
    {DRIVER_CAN_IVIEW,       "CAN_i-VIEW.dll",          "&i-VIEW"           },
    {DRIVER_CAN_ICS_NEOVI,   "CAN_ICS_neoVI.dll",       "IntrepidCS ne&oVI"  },
    {DRIVER_CAN_IXXAT,       "CAN_IXXAT_VCI.dll",       "I&XXAT VCI"        },
    {DRIVER_CAN_KVASER_CAN,  "CAN_Kvaser_CAN.dll",      "&Kvaser CAN"       },
    {DRIVER_CAN_MHS,         "CAN_MHS.dll",             "&MHS Tiny-CAN"     },
    {DRIVER_CAN_NSI,         "CAN_NSI.dll",             "&NSI CAN-API"      },
    {DRIVER_CAN_PEAK_USB,    "CAN_PEAK_USB.dll",        "&PEAK USB"         },
    {DRIVER_CAN_VECTOR_XL,   "CAN_Vector_XL.dll",       "&Vector XL"        },
    {DRIVER_CAN_VSCOM,       "CAN_VSCOM.dll",           "VScom &CAN-API"    },
};

/*
 * LIN driver catalog in menu order: deactivation first, all other drivers
 * in alphabetical order.
 */
static const SDILDriverEntry sg_asDriverCatalogLIN[] =
{
    {(unsigned long)DAL_LIN_NONE,  nullptr,                 "&Deactivate"       },
    {DRIVER_LIN_ETAS_BOA,      "LIN_ETAS_BOA.dll",      "ETAS &BOA"         },
    {DRIVER_LIN_ISOLAR_EVE_VLIN, "LIN_ISOLAR_EVE_VLIN.dll", "ETAS &ISOLAR-EVE"},
    {DRIVER_LIN_PEAK_USB,      "LIN_PEAK_USB.dll",      "&PEAK USB"         },
    {DRIVER_LIN_VECTOR_XL,     "LIN_Vector_XL.dll",     "&Vector XL"        },
    {DRIVER_LIN_KVASER,        "LIN_Kvaser.dll",        "&Kvaser LIN"       },
};