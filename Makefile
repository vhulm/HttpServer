all: httpd

httpd: httpd.c
	gcc  -o httpd httpd.c -lpthread

clean:
	rm httpd
