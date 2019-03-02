SET(DAEMON account-svcd)

INCLUDE(FindPkgConfig)
pkg_check_modules(pkgs REQUIRED
		dlog
		db-util
		glib-2.0
		capi-base-common
		capi-system-info
		pkgmgr-info
		gio-2.0
		vconf
		cynara-client
		cynara-session
		cynara-creds-gdbus
		account-common
)

FOREACH(flag ${pkgs_CFLAGS})
	SET(EXTRA_CFLAGS "${EXTRA_CFLAGS} ${flag}")
ENDFOREACH(flag)

SET(SERVER_SRCS
	src/account-server.c
	src/account-server-db.c
	src/lifecycle.c
)

INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR}/server/include)

SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${EXTRA_CFLAGS} -Wall -Werror -Wno-int-conversion")
SET(CMAKE_LDFLAGS "-Wl,-zdefs")

ADD_EXECUTABLE(${DAEMON} ${SERVER_SRCS})

TARGET_LINK_LIBRARIES(${DAEMON} ${pkgs_LDFLAGS})

INSTALL(TARGETS ${DAEMON} DESTINATION bin)

