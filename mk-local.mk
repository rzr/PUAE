#! /usr/bin/make -f

generated_files=\
 src/tools/gencpu \
 src/uae \
#}

distclean:
	-make $@
	-make clean
	find . -type l -exec rm -fv '{}' ;\
	find . -type f -iname "*.o" -exec rm -fv '{}' \;
	rm -f ${generated_files}