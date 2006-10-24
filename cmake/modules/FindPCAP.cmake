# - Find pcap
# Find the PCAP includes and library
#
#  PCAP_INCLUDE_DIRS - where to find pcap.h, etc.
#  PCAP_LIBRARIES   - List of libraries when using pcap.
#  PCAP_FOUND       - True if pcap found.

#Includes
FIND_PATH(PCAP_INCLUDE_DIR pcap.h
  /usr/local/include
  /usr/include
)

SET(PCAP_INCLUDE_DIRS ${PCAP_INCLUDE_DIR})

#Library
FIND_LIBRARY(PCAP_LIBRARY
  NAMES pcap
  PATHS /usr/lib /usr/local/lib
)

SET(PCAP_LIBRARIES ${PCAP_LIBRARY})

#Functions
INCLUDE(CheckFunctionExists)
SET(CMAKE_REQUIRED_INCLUDES ${PCAP_INCLUDE_DIRS})
SET(CMAKE_REQUIRED_LIBRARIES ${PCAP_LIBRARIES})
CHECK_FUNCTION_EXISTS("pcap_breakloop" HAVE_PCAP_BREAKLOOP)
CHECK_FUNCTION_EXISTS("pcap_datalink_name_to_val" HAVE_PCAP_DATALINK_NAME_TO_VAL)
CHECK_FUNCTION_EXISTS("pcap_datalink_val_to_name" HAVE_PCAP_DATALINK_VAL_TO_NAME)
CHECK_FUNCTION_EXISTS("pcap_findalldevs" HAVE_PCAP_FINDALLDEVS)
CHECK_FUNCTION_EXISTS("pcap_freecode" HAVE_PCAP_FREECODE)
CHECK_FUNCTION_EXISTS("pcap_get_selectable_fd" HAVE_PCAP_GET_SELECTABLE_FD)
CHECK_FUNCTION_EXISTS("pcap_lib_version" HAVE_PCAP_LIB_VERSION)
CHECK_FUNCTION_EXISTS("pcap_list_datalinks" HAVE_PCAP_LIST_DATALINKS)
CHECK_FUNCTION_EXISTS("pcap_open_dead" HAVE_PCAP_OPEN_DEAD)
CHECK_FUNCTION_EXISTS("pcap_set_datalink" HAVE_PCAP_SET_DATALINK)


#Is pcap found ?
IF(PCAP_INCLUDE_DIR AND PCAP_LIBRARY)
  SET( PCAP_FOUND "YES" )
ENDIF(PCAP_INCLUDE_DIR AND PCAP_LIBRARY)


MARK_AS_ADVANCED(
  PCAP_LIBRARY
  PCAP_INCLUDE_DIR
)
