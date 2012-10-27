objects = pigmap.o blockimages.o chunk.o map.o render.o region.o rgba.o tables.o utils.o world.o

ifeq ($(mode),debug)
	CFLAGS = -g -Wall -D_DEBUG
else
	CFLAGS = -Wall -O3 -DNDEBUG
endif

pigmap : $(objects)
	g++ $(objects) -o pigmap -l z -l png -l jpeg -l pthread $(CFLAGS)

pigmap.o : pigmap.cpp blockimages.h chunk.h map.h render.h rgba.h tables.h utils.h world.h
	g++ -c pigmap.cpp $(CFLAGS)
blockimages.o : blockimages.cpp blockimages.h rgba.h utils.h
	g++ -c blockimages.cpp $(CFLAGS)
chunk.o : chunk.cpp chunk.h map.h region.h tables.h utils.h
	g++ -c chunk.cpp $(CFLAGS)
map.o : map.cpp map.h utils.h
	g++ -c map.cpp $(CFLAGS)
render.o : render.cpp blockimages.h chunk.h map.h render.h rgba.h tables.h utils.h
	g++ -c render.cpp $(CFLAGS)
region.o : region.cpp map.h region.h tables.h utils.h
	g++ -c region.cpp $(CFLAGS)
rgba.o : rgba.cpp rgba.h utils.h
	g++ -c rgba.cpp $(CFLAGS)
tables.o : tables.cpp map.h tables.h utils.h
	g++ -c tables.cpp $(CFLAGS)
utils.o : utils.cpp utils.h
	g++ -c utils.cpp $(CFLAGS)
world.o : world.cpp map.h region.h tables.h world.h
	g++ -c world.cpp $(CFLAGS)

clean :
	rm -f *.o pigmap
	
