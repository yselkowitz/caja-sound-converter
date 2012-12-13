gsettingsconvertdir = $(datadir)/MateConf/gsettings
gsettingsconvert_DATA = caja-sound-converter.convert

gsettingsschema_in_files = org.mate.caja-sound-converter.gschema.xml.in
gsettings_SCHEMAS = $(gsettingsschema_in_files:.xml.in=.xml)
.PRECIOUS: $(gsettings_SCHEMAS)

@INTLTOOL_XML_RULE@

@GSETTINGS_RULES@

builderdir = $(datadir)/caja-sound-converter
builder_DATA =		\
	main.ui		\
	progress.ui	\
	rhythmbox.gep
	
EXTRA_DIST =			\
	$(builder_DATA)		\
	$(gsettingsschema_in_files)

CLEANFILES =	\
	$(gsettings_SCHEMAS)
