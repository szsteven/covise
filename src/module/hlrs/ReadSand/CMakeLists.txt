SET(HEADERS
)
SET(SOURCES
  ReadSand.cpp
)
ADD_COVISE_MODULE(IO ReadSand ${EXTRASOURCES})
TARGET_LINK_LIBRARIES(ReadSand coApi coAppl coCore)
COVISE_INSTALL_TARGET(ReadSand)
