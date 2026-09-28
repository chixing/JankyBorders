FILES = src/main.c src/parse.c src/mach.c src/hashtable.c src/events.c src/windows.c src/border.c src/animation.c 
LIBS = -framework AppKit -framework CoreVideo -F/System/Library/PrivateFrameworks/ -framework SkyLight

PREFIX ?= $(HOME)/.local
CERT ?= $(shell security find-identity -v -p codesigning | sed -n 's/.*"\(Apple Development: [^"]*\)".*/\1/p' | head -n 1)

all: | bin
	clang -std=c99 -O3 -g $(FILES) -o bin/borders $(LIBS)
	clang -std=c99 -O2 -Wall src/borders_msg.c -o bin/borders-msg

# Replaces the binaries atomically so a running borders keeps its old image.
install: all
	mkdir -p $(PREFIX)/bin
	for f in borders borders-msg; do \
	  cp bin/$$f $(PREFIX)/bin/.$$f.new && \
	  codesign -fs "$(or $(CERT),-)" $(PREFIX)/bin/.$$f.new && \
	  mv -f $(PREFIX)/bin/.$$f.new $(PREFIX)/bin/$$f || exit 1; \
	done

debug: | bin
	clang -std=c99 -O0 -g -DDEBUG $(FILES) -o bin/debug $(LIBS)

asan: | bin
	clang -std=c99 -Wall -g -fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer -g $(FILES) -o bin/debug $(LIBS)
	./bin/debug

bin:
	mkdir bin

clean:
	rm -rf bin
