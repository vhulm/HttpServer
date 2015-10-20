all: httpd

httpd: httpd.c
	gcc -W -Wall httpd.c -lpthread -o httpd

clean:
	rm httpd
