#
# Copyright (c) 2023 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

target_sources(app PRIVATE
	       ${CMAKE_CURRENT_SOURCE_DIR}/main.c
	       ${CMAKE_CURRENT_SOURCE_DIR}/auracast_gatt.c
	       ${CMAKE_CURRENT_SOURCE_DIR}/hid_stub.c)
