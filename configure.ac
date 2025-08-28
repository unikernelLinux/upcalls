# Upcall experiments
# Copyright (C) 2025 Boston University,
# Eric B. Munson
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

AC_INIT([UPCALLS], 1.0.0)

dnl This must be used very early else you will get
dnl "warning: AC_RUN_IFELSE was called before AC_USE_SYSTEM_EXTENSIONS"
m4_ifdef([AC_USE_SYSTEM_EXTENSIONS],[],
         [m4_define([AC_USE_SYSTEM_EXTENSIONS],[])])
AC_USE_SYSTEM_EXTENSIONS

dnl NB: Do not [quote] this parameter.
AM_INIT_AUTOMAKE(foreign)

dnl Check for basic C environment.
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_CPP
AC_SYS_LARGEFILE

AC_C_PROTOTYPES
test "x$U" != "x" && AC_MSG_ERROR([Compiler not ANSI compliant])

AM_PROG_CC_C_O

dnl Misc programs.
AC_PROG_SED

dnl Select the target program.
AC_MSG_CHECKING([which program to compile])
AC_ARG_WITH([program],
    [AS_HELP_STRING([--with-program],
	[set which program to compile @<:@default=event-tester@:>@])],
	[PROGRAM="$withval"],
	[PROGRAM="event-tester"])
AC_MSG_RESULT([$PROGRAM])
AC_SUBST([PROGRAM])


dnl Set the string used to start the experiment
AC_MSG_CHECKING([Target Application string])
AC_ARG_WITH([app-trigger],
    [AS_HELP_STRING([--with-app-trigger],
	[set the string checked in exec to trigger the target process @<:@default=/usr/bin/tcp_echo@:>@])],
	[TRIGGER="$withval"],
	[TRIGGER="/usr/bin/tcp_echo"])
AC_MSG_RESULT([$TRIGGER])
AC_SUBST([TRIGGER])

dnl Start perf before experiment process
AC_MSG_CHECKING([perf invocation])
AC_ARG_WITH([profiler],
    [AS_HELP_STRING([--with-profiler],
	[run this profiler before starting experiment process @<:@default=@:>@])],
	[PROFILER="$withval"],
	[PROFILER=""])
AC_MSG_RESULT([$PROFILER])
AC_SUBST([PROFILER])

AC_MSG_CHECKING([if we should enable and use libupcall])
AC_ARG_ENABLE([libupcall],
    [AS_HELP_STRING([--enable-libupcall],
	[enable libupcall @<:@default=no@:>@])],
	[ENABLE_LIBUPCALL="$enableval"],
	[ENABLE_LIBUPCALL="no"])
AM_CONDITIONAL([ENABLE_LIBUPCALL], [test "x$ENABLE_LIBUPCALL" = "xyes"])
AC_MSG_RESULT([$ENABLE_LIBUPCALL])

dnl Use the selected event system for tcp_echo
AC_MSG_CHECKING([event system])
AC_ARG_WITH([event_system],
    [AS_HELP_STRING([--with-event-system],
        [use the selected event system @<:@default=epoll@:>@])],
        [EVENT_SYSTEM="$withval"],
        [EVENT_SYSTEM="epoll"])
AC_MSG_RESULT([$EVENT_SYSTEM])
AC_SUBST([EVENT_SYSTEM])

AM_CONDITIONAL(EV_IS_URING, [test x"$EVENT_SYSTEM" = xiouring])

dnl Produce output files.
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([Makefile \
		 initrd/init \
		 initrd/Makefile \
		 libupcall/Makefile \
		 event-tester/Makefile])
AC_OUTPUT
