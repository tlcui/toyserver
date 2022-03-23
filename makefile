server: *.cpp
	g++ -o server *.cpp -lpthread -lmysqlclient -DNDEBUG -O2 -w

clean:
	rm -r server
