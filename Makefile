all: httpd

httpd: httpd.c
	gcc -W -Wall httpd.c -lpthread -lrt -o httpd

clean:
	rm httpd
