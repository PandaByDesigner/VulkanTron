noinst_LIBRARIES=libinput.a

libinput_a_SOURCES= \
	system_keynames.c input_system.c

EXTRA_DIST = sdl12_unicode_keymap.inc

AM_CFLAGS = \
	-I$(top_srcdir)/lua/include \
	-I$(top_srcdir)/nebu/include
