CC_SDL=`sdl2-config --cflags --libs`
LDFLAGS  =
CXXFLAGS = -I. -I./examples -O3 -DNDEBUG -std=c++11 -fPIC
CXX = cc







ggml.o: src/ggml.c src/ggml.h src/ggml-cuda.h
	$(CC)  $(CFLAGS)   -c $< -o $@
whisper.o: src/whisper.cpp src/whisper.h src/ggml.h src/ggml-cuda.h
	$(CXX) $(CXXFLAGS) -c $< -o $@




bolo: src/bolo.cpp  src/gpt-2.cpp ggml.o whisper.o
	$(CXX) $(CXXFLAGS) src/bolo.cpp src/gpt-2.cpp ggml.o whisper.o -o talk $(CC_SDL) $(LDFLAGS)


clean:
	rm -f *.o talk





