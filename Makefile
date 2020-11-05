OPTS=-Wall -Werror -g -std=c11
CC=cc

bin/ccms: obj/sqlite3.o \
	obj/mongoose.o \
	obj/ctemplate.o \
	obj/main.o \
	obj/initial.sql.o
	$(CC) $(OPTS) -o bin/ccms \
		obj/sqlite3.o \
		obj/mongoose.o \
		obj/ctemplate.o \
		obj/main.o \
		obj/initial.sql.o \
		-lpthread -ldl

obj/sqlite3.o: src/thirdparty/sqlite3/sqlite3.c src/thirdparty/sqlite3/sqlite3.h
	$(CC) $(OPTS) -o obj/sqlite3.o -c src/thirdparty/sqlite3/sqlite3.c -lpthread -ldl

obj/mongoose.o: src/thirdparty/mongoose/mongoose.c src/thirdparty/mongoose/mongoose.h
	$(CC) $(OPTS) -o obj/mongoose.o -c src/thirdparty/mongoose/mongoose.c

# libctemplate fails with -Wall -Werror
obj/ctemplate.o: src/thirdparty/ctemplate/ctemplate.c src/thirdparty/ctemplate/ctemplate.h
	$(CC) -o obj/ctemplate.o -c src/thirdparty/ctemplate/ctemplate.c -I src/thirdparty/ctemplate

obj/main.o: src/main.c
	$(CC) $(OPTS) -o obj/main.o -c src/main.c -I src/thirdparty/mongoose -I src/thirdparty/sqlite3

# Database initialization script, create object file from the raw SQL script.
obj/initial.sql.o: src/initial.sql
	ld -r -b binary -o obj/initial.sql.o src/initial.sql

clean:
	rm -rf bin/* obj/* ccms.db

install:
	cp bin/ccms /usr/local/bin/

uninstall:
	rm /usr/local/bin/ccms
