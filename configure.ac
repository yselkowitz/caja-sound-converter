AC_INIT([caja-sound-converter], [1.5.0],
        [https://github.com/cygwinports/mate-file-manager-sound-converter/issues],
        [mate-file-manager-sound-converter])
AC_PREREQ([2.59])
AC_COPYRIGHT([Copyright (C) 2008-2010 Brian Pepple])

AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_SRCDIR([Makefile.am])
AC_CONFIG_HEADERS(config.h)
AM_INIT_AUTOMAKE([1.11 dist-xz no-dist-gzip check-news])
AM_SILENT_RULES([yes])
AM_MAINTAINER_MODE

dnl -----------------------------------------------------------
dnl Checks for programs.
dnl -----------------------------------------------------------
AC_PROG_CC
IT_PROG_INTLTOOL([0.35.0])
AC_DISABLE_STATIC
AC_PROG_LIBTOOL

GLIB_GSETTINGS

MATE_COMPILE_WARNINGS([maximum])
MATE_CXX_WARNINGS
MATE_DEBUG_CHECK
MATE_MAINTAINER_MODE_DEFINES

dnl -----------------------------------------------------------
dnl Set variables for minimum versions needed.
dnl -----------------------------------------------------------
GLIB_REQUIRED=2.26.0
CAJA_REQUIRED=1.2.0
GTK_REQUIRED=2.16.0
GSTREAMER_REQUIRED=1.0.0

dnl -----------------------------------------------------------
dnl Check for required libraries
dnl -----------------------------------------------------------
PKG_CHECK_MODULES(NSC,
[
	glib-2.0 >= $GLIB_REQUIRED
	gio-2.0
	libcaja-extension >= $CAJA_REQUIRED
	gtk+-2.0 >= $GTK_REQUIRED
	gstreamer-1.0 >= $GSTREAMER_REQUIRED
	gstreamer-pbutils-1.0
	gstreamer-plugins-base-1.0
])
AC_SUBST(NSC_CFLAGS)
AC_SUBST(NSC_LIBS)

dnl -----------------------------------------------------------
dnl Get the correct caja extensions directory
dnl -----------------------------------------------------------
AC_ARG_WITH(cajadir,
	    AS_HELP_STRING([--with-cajadir=DIR],[Installation path for Caja extension @<:@auto@:>@]),
	    [ac_with_cajadir=$withval],
	    [ac_with_cajadir=""])
if test "${ac_with_cajadir}" = ""; then
	ac_with_cajadir=`pkg-config --variable=extensiondir libcaja-extension`
fi

AC_MSG_NOTICE([installing caja plugin in ${ac_with_cajadir}])
AC_SUBST([CAJA_EXTENSION_DIR],[${ac_with_cajadir}])


dnl -----------------------------------------------------------
dnl Find the GStreamer elements we use, and warn if they are
dnl not present.
dnl -----------------------------------------------------------
GST_ELEMENT_CHECK(vorbisdec,1.0,,AC_MSG_WARN([The 'vorbisdec' element was not found. This will cause decoding from Ogg Vorbis to fail.]))
GST_ELEMENT_CHECK(vorbisenc,1.0,,AC_MSG_WARN([The 'vorbisenc' element was not found. This will cause encoding to Ogg Vorbis to fail.]))
GST_ELEMENT_CHECK(flacdec,1.0,,AC_MSG_WARN([The 'flacdec' element was not found. This will cause decoding from FLAC to fail.]))
GST_ELEMENT_CHECK(flacenc,1.0,,AC_MSG_WARN([The 'flacenc' element was not found. This will cause encoding to FLAC to fail.]))
GST_ELEMENT_CHECK(wavpackenc,1.0,,AC_MSG_WARN([The 'wavpackenc' element was not found. This will cause encoding from Wav to fail.]))
GST_ELEMENT_CHECK(wavpackdec,1.0,,AC_MSG_WARN([The 'wavpackdec' element was not found. This will cause decoding to Wav to fail.]))
GST_ELEMENT_CHECK(giosink,1.0,,AC_MSG_WARN([The 'giosink' element was not found. This will cause Caja-Sound-Converter to fail at runtime.]))
GST_ELEMENT_CHECK(giosrc,1.0,,AC_MSG_WARN([The 'giosrc' element was not found. This will cause Caja-Sound-Converter to fail at runtime.]))
GST_ELEMENT_CHECK(decodebin,1.0,,AC_MSG_WARN([The 'decodebin' element was not found. This will cause Caja-Sound-Converter to fail at runtime.]))
GST_ELEMENT_CHECK(audioresample,1.0,,AC_MSG_WARN([The 'audioresample' element was not found.  This will cause Caja-Sound-Converter to fail at runtime.]))
GST_ELEMENT_CHECK(audioconvert,1.0,,AC_MSG_WARN([The 'audioconvert' element was not found. This will cause Caja-Sound-Converter to fail at runtime.]))


dnl -----------------------------------------------------------
dnl Language Support
dnl -----------------------------------------------------------
GETTEXT_PACKAGE=AC_PACKAGE_NAME
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED([GETTEXT_PACKAGE],["$GETTEXT_PACKAGE"],[Gettext package])
AM_GLIB_GNU_GETTEXT

dnl -----------------------------------------------------------
dnl Write Makefiles
dnl -----------------------------------------------------------
AC_CONFIG_FILES([
	Makefile
	data/Makefile
	src/Makefile
	po/Makefile.in
])

AC_OUTPUT

echo
echo $PACKAGE $VERSION
echo
echo "Prefix:   $prefix"
