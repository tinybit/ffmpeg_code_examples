.PHONY: all

all: example2

example1:
	g++ -std=c++11 -O3 01-remuxing.cpp -lsrt -lpthread -lz -ldl -lswresample -lm -lva -lva-drm /usr/lib64/libavformat.a /usr/lib64/libavcodec.a /usr/lib64/libx264.a /usr/lib64/libswresample.a /usr/lib64/libavutil.a /usr/lib64/libfdk-aac.a -o remux

example2:
	g++ -std=c++11 -O3 02-reading-from-memory.cpp ring_buffer.cpp -I/usr/include/srt -lsrt -lpthread -lcrypto -lz -ldl -lswresample -lm -lva -lva-drm -lstdc++ /usr/lib64/libavformat.a /usr/lib64/libavcodec.a /usr/lib64/libx264.a /usr/lib64/libswresample.a /usr/lib64/libswscale.a /usr/lib64/libx264.a /usr/lib64/libavutil.a /usr/lib64/libfdk-aac.a -o read_from_memory

clean:
	rm remux read_from_memory
