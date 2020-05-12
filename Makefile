objects = pigmap.o blockimages.o chunk.o map.o render.o region.o rgba.o tables.o utils.o world.o

ifeq ($(mode),debug)
	CFLAGS = -g -Wall -D_DEBUG
else ifeq ($(mode),profile)
	CFLAGS = -Wall -O3 -DNDEBUG -pg
else ifeq ($(mode),coverage)
	CFLAGS = -Wall -O3 -DNDEBUG -fprofile-arcs -ftest-coverage
else
	CFLAGS = -Wall -O3 -DNDEBUG
endif

pigmap : $(objects)
	$(CXX) $(objects) -o pigmap -l z -l png -l jpeg -l pthread $(CFLAGS)

pigmap.o : pigmap.cpp blockimages.h chunk.h map.h render.h rgba.h tables.h utils.h world.h
	$(CXX) -c pigmap.cpp $(CFLAGS)
blockimages.o : blockimages.cpp blockimages.h rgba.h utils.h
	$(CXX) -c blockimages.cpp $(CFLAGS) -std=c++0x
chunk.o : chunk.cpp chunk.h map.h region.h tables.h utils.h
	$(CXX) -c chunk.cpp $(CFLAGS)
map.o : map.cpp map.h utils.h
	$(CXX) -c map.cpp $(CFLAGS)
render.o : render.cpp blockimages.h chunk.h map.h render.h rgba.h tables.h utils.h
	$(CXX) -c render.cpp $(CFLAGS)
region.o : region.cpp map.h region.h tables.h utils.h
	$(CXX) -c region.cpp $(CFLAGS)
rgba.o : rgba.cpp rgba.h utils.h
	$(CXX) -c rgba.cpp $(CFLAGS)
tables.o : tables.cpp map.h tables.h utils.h
	$(CXX) -c tables.cpp $(CFLAGS)
utils.o : utils.cpp utils.h
	$(CXX) -c utils.cpp $(CFLAGS)
world.o : world.cpp map.h region.h tables.h world.h
	$(CXX) -c world.cpp $(CFLAGS)

clean :
	rm -f *.o pigmap
