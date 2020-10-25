bin/ccms: obj/sqlite3.o obj/mongoose.o obj/ctemplate.o obj/ccms.o obj/main.o
	cc -o bin/ccms obj/sqlite3.o obj/mongoose.o obj/ctemplate.o obj/ccms.o obj/main.o -lpthread -ldl

obj/sqlite3.o: src/thirdparty/sqlite3/sqlite3.c src/thirdparty/sqlite3/sqlite3.h
	cc -o obj/sqlite3.o -c src/thirdparty/sqlite3/sqlite3.c -lpthread -ldl

obj/mongoose.o: src/thirdparty/mongoose/mongoose.c src/thirdparty/mongoose/mongoose.h
	cc -o obj/mongoose.o -c src/thirdparty/mongoose/mongoose.c

obj/ctemplate.o: src/thirdparty/ctemplate/ctemplate.c src/thirdparty/ctemplate/ctemplate.h
	cc -o obj/ctemplate.o -c src/thirdparty/ctemplate/ctemplate.c -I src/thirdparty/ctemplate

obj/ccms.o: src/ccms.c include/ccms.h
	cc -o obj/ccms.o -c src/ccms.c

obj/main.o: src/main.o
	cc -o obj/main.o -c src/main.c

clean:
	rm -rf bin/* obj/*

install:
	cp bin/ccms /usr/local/bin/

uninstall:
	rm /usr/local/bin/ccms
