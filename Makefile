.PHONY: all

all:
	g++ -std=c++11 -O3 remuxing.cpp -lsrt -lpthread -lz -ldl -lswresample -lm -lva -lva-drm /usr/lib64/libavformat.a /usr/lib64/libavcodec.a /usr/lib64/libx264.a /usr/lib64/libswresample.a /usr/lib64/libavutil.a /usr/lib64/libfdk-aac.a -o remux

run:
	./test 0.0.0.0 9999
