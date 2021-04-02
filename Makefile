OPTS=-Wall -Werror -g -std=c99

bin/ccms: obj/mustach.o \
	obj/main.o \
	obj/initial.sql.o \
	obj/editor.html.o
	$(CC) $(OPTS) -o bin/ccms \
		obj/mustach.o \
		obj/main.o \
		obj/initial.sql.o \
		obj/editor.html.o \
		-lpthread \
		-ldl \
		-lcmark \
		-lsqlite3 \
		-ljson-c \
		-lmicrohttpd 

obj/mustach.o: src/thirdparty/mustach/mustach.c src/thirdparty/mustach/mustach.h
	$(CC) $(OPTS) -o obj/mustach.o \
		-c src/thirdparty/mustach/mustach.c \
		-I src/thirdparty/mustach

obj/main.o: src/main.c
	$(CC) $(OPTS) -o obj/main.o \
		-c src/main.c \
		-I src/thirdparty/mustach \
		-I src/thirdparty/danielgibson

obj/initial.sql.o: src/initial.sql
	ld -r -b binary -o obj/initial.sql.o src/initial.sql

obj/editor.html.o: src/editor.html
	ld -r -b binary -o obj/editor.html.o src/editor.html

clean:
	rm -rf bin/* obj/* ccms.db

install:
	cp bin/ccms /usr/local/bin/

uninstall:
	rm /usr/local/bin/ccms
