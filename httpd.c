/* J. David's webserver */
/* This is a simple webserver.
 * Created November 1999 by J. David Blackstone.
 * CSE 4344 (Network concepts), Prof. Zeigler
 * University of Texas at Arlington
 */
/* This program compiles for Sparc Solaris 2.6.
 * To compile for Linux:
 *  1) Comment out the #include <pthread.h> line.
 *  2) Comment out the line that defines the variable newthread.
 *  3) Comment out the two lines that run pthread_create().
 *  4) Uncomment the line that runs accept_request().
 *  5) Remove -lsocket from the Makefile.
 */
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctype.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <fcntl.h>

//printf("%s\n%s\n%s\n%s\n",request->Method,request->URL,request->Path,request->Query);
//while(1);


#define MAXBUFSIZE (4096)

#define ISspace(x) isspace((int)(x))

#define LOGFILE_DIR "./log.txt"
#define SERVER_STRING "Server: HttpServer/0.1.0\r\n"

#define MAXERRORLISTNUM 4
#define RESPONSE_NO_ERROR(STATUS) ((STATUS)!=(-1))
#define CGI_FILE (1)
#define ISCGI_FILE(type)  ((type)!=(0))


//以下三个结构必须被正确初始化
const int ErrorMap[MAXERRORLISTNUM]={001,200,404,501};

const char * const ErrorDes[MAXERRORLISTNUM]={	"001", //001
									"OK",	//200
									"This web page not found!",//404
									"501", //501
									};
					
const char * const ErrorFile[MAXERRORLISTNUM]={	"001.html",
									NULL,
									"htdocs/404.html",
									"htdocs/501.html",
									};

typedef struct
{
	char ProtocolVersion[10];
	int StatusCode;
	char *Des;
	char ContentType[100];
	long int ContentLength;
}RESPONSE_STATIC_MSG;

typedef struct
{
	int i;
}RESPONSE_CGI_MSG;

typedef struct
{
	int client;
	int ParseState;
	int ErrorCode;
	char Method[10];
	char URL[1024];
	char Path[1024];
	char *Query;
	RESPONSE_STATIC_MSG StaticMsg;
	RESPONSE_CGI_MSG CgiMsg;
}RESPONSE_MSG;

void accept_request(int);
void bad_request(int);
void cat(int, FILE *);
void cannot_execute(int);
void error_die(const char *);
void execute_cgi(int, const char *, const char *, const char *);
int get_line(int, char *, int);

int Startup(u_short *);
void Deal_Request(int client);
char *Get_ErrorDes(int StatusCode);	//根据错误码返回http响应行描述信息，如果列表找不到返回第一条记录
char *Get_ErrorFileFd(int StatusCode);//根据错误码返回错误页路径，如果列表找不到返回第一条记录
int ParseRequest(int client,RESPONSE_MSG *request);
int CheckRequest(RESPONSE_MSG *request);
int ResponseClient(RESPONSE_MSG *request);

int Send_ResponseLineToClient(int client,int statusCode,const char *des);
int Send_ResponseHeadToClient(int client,const char *headName,const char *value);
int Send_ResponseBlankLineToClient(int client);
int Send_ResponseBodyToClient(int client,const char *path);

int WriteLogtoFile(int errno,const char *msg);


/**********************************************************************/
/* Inform the client that a request it has made has a problem.
 * Parameters: client socket */
/**********************************************************************/
void bad_request(int client)
{
 char buf[1024];

 sprintf(buf, "HTTP/1.0 400 BAD REQUEST\r\n");
 send(client, buf, sizeof(buf), 0);
 sprintf(buf, "Content-type: text/html\r\n");
 send(client, buf, sizeof(buf), 0);
 sprintf(buf, "\r\n");
 send(client, buf, sizeof(buf), 0);
 sprintf(buf, "<P>Your browser sent a bad request, ");
 send(client, buf, sizeof(buf), 0);
 sprintf(buf, "such as a POST without a Content-Length.\r\n");
 send(client, buf, sizeof(buf), 0);
}

/**********************************************************************/
/* Put the entire contents of a file out on a socket.  This function
 * is named after the UNIX "cat" command, because it might have been
 * easier just to do something like pipe, fork, and exec("cat").
 * Parameters: the client socket descriptor
 *             FILE pointer for the file to cat */
/**********************************************************************/
void cat(int client, FILE *resource)
{
 char buf[1024];

 fgets(buf, sizeof(buf), resource);
 while (!feof(resource))
 {
  send(client, buf, strlen(buf), 0);
  fgets(buf, sizeof(buf), resource);
 }
}

/**********************************************************************/
/* Inform the client that a CGI script could not be executed.
 * Parameter: the client socket descriptor. */
/**********************************************************************/
void cannot_execute(int client)
{
 char buf[1024];

 sprintf(buf, "HTTP/1.0 500 Internal Server Error\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "Content-type: text/html\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "<P>Error prohibited CGI execution.\r\n");
 send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Print out an error message with perror() (for system errors; based
 * on value of errno, which indicates system call errors) and exit the
 * program indicating an error. */
/**********************************************************************/
void error_die(const char *sc)
{
 perror(sc);
 exit(1);
}

/**********************************************************************/
/* Execute a CGI script.  Will need to set environment variables as
 * appropriate.
 * Parameters: client socket descriptor
 *             path to the CGI script */
/**********************************************************************/
void execute_cgi(int client, const char *path,
                 const char *method, const char *query_string)
{
 char buf[1024];
 int cgi_output[2];
 int cgi_input[2];
 pid_t pid;
 int status;
 int i;
 char c;
 int numchars = 1;
 int content_length = -1;

 buf[0] = 'A'; buf[1] = '\0';
 if (strcasecmp(method, "GET") == 0)
  while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
   numchars = get_line(client, buf, sizeof(buf));
 else    /* POST */
 {
  numchars = get_line(client, buf, sizeof(buf));
  while ((numchars > 0) && strcmp("\n", buf))
  {
   buf[15] = '\0';
   if (strcasecmp(buf, "Content-Length:") == 0)
    content_length = atoi(&(buf[16]));
   numchars = get_line(client, buf, sizeof(buf));
  }
  if (content_length == -1) {
   bad_request(client);
   return;
  }
 }

 sprintf(buf, "HTTP/1.0 200 OK\r\n");
 send(client, buf, strlen(buf), 0);

 if (pipe(cgi_output) < 0) {
  cannot_execute(client);
  return;
 }
 if (pipe(cgi_input) < 0) {
  cannot_execute(client);
  return;
 }

 if ( (pid = fork()) < 0 ) {
  cannot_execute(client);
  return;
 }
 if (pid == 0)  /* child: CGI script */
 {
  char meth_env[255];
  char query_env[255];
  char length_env[255];

  dup2(cgi_output[1], 1);
  dup2(cgi_input[0], 0);
  close(cgi_output[0]);
  close(cgi_input[1]);
  sprintf(meth_env, "REQUEST_METHOD=%s", method);
  putenv(meth_env);
  if (strcasecmp(method, "GET") == 0) {
   sprintf(query_env, "QUERY_STRING=%s", query_string);
   putenv(query_env);
  }
  else {   /* POST */
   sprintf(length_env, "CONTENT_LENGTH=%d", content_length);
   putenv(length_env);
  }
  execl(path, path, NULL);
  exit(0);
 } else {    /* parent */
  close(cgi_output[1]);
  close(cgi_input[0]);
  if (strcasecmp(method, "POST") == 0)
   for (i = 0; i < content_length; i++) {
    recv(client, &c, 1, 0);
    write(cgi_input[1], &c, 1);
   }
  while (read(cgi_output[0], &c, 1) > 0)
   send(client, &c, 1, 0);

  close(cgi_output[0]);
  close(cgi_input[1]);
  waitpid(pid, &status, 0);
 }
}

/**********************************************************************/
/* Get a line from a socket, whether the line ends in a newline,
 * carriage return, or a CRLF combination.  Terminates the string read
 * with a null character.  If no newline indicator is found before the
 * end of the buffer, the string is terminated with a null.  If any of
 * the above three line terminators is read, the last character of the
 * string will be a linefeed and the string will be terminated with a
 * null character.
 * Parameters: the socket descriptor
 *             the buffer to save the data in
 *             the size of the buffer
 * Returns: the number of bytes stored (excluding null) */
/**********************************************************************/
int get_line(int sock, char *buf, int size)
{
 int i = 0;
 char c = '\0';
 int n;

 while ((i < size - 1) && (c != '\n'))
 {
  n = recv(sock, &c, 1, 0);
  /* DEBUG printf("%02X\n", c); */
  if (n > 0)
  {
   if (c == '\r')
   {
    n = recv(sock, &c, 1, MSG_PEEK);
    /* DEBUG printf("%02X\n", c); */
    if ((n > 0) && (c == '\n'))
     recv(sock, &c, 1, 0);
    else
     c = '\n';
   }
   buf[i] = c;
   i++;
  }
  else
   c = '\n';
 }
 buf[i] = '\0';
 
 return(i);
}

/**********************************************************************/
/* This function starts the process of listening for web connections
 * on a specified port.  If the port is 0, then dynamically allocate a
 * port and modify the original port variable to reflect the actual
 * port.
 * Parameters: pointer to variable containing the port to connect on
 * Returns: the socket */
/**********************************************************************/
int Startup(u_short *port)
{
 int httpd = 0;
 struct sockaddr_in name;

 httpd = socket(PF_INET, SOCK_STREAM, 0);
 if (httpd == -1)
  error_die("socket");
 memset(&name, 0, sizeof(name));
 name.sin_family = AF_INET;
 name.sin_port = htons(*port);
 name.sin_addr.s_addr = htonl(INADDR_ANY);
int on=1;
if(setsockopt(httpd,SOL_SOCKET,SO_REUSEADDR,&on,sizeof(on))<0)
{
	printf("setsockopt error!\n");	
	exit(-5);
}
 if (bind(httpd, (struct sockaddr *)&name, sizeof(name)) < 0)
  error_die("bind");
 if (*port == 0)  /* if dynamically allocating a port */
 {
  int namelen = sizeof(name);
  if (getsockname(httpd, (struct sockaddr *)&name, &namelen) == -1)
   error_die("getsockname");
  *port = ntohs(name.sin_port);
 }
 if (listen(httpd, 5) < 0)
  error_die("listen");
 return(httpd);
}

/**********************************************************************/
void Deal_Request(int client)
{
	RESPONSE_MSG msg_client;
	memset(&msg_client,0,sizeof(msg_client));
	ParseRequest(client,&msg_client);
	CheckRequest(&msg_client);
	ResponseClient(&msg_client);
	printf("%s\n%s\n%s\n%s\n",msg_client.Method,msg_client.URL,msg_client.Path,msg_client.Query);
 	close(msg_client.client);
}



int ParseRequest(int client,RESPONSE_MSG *request)
{
	/*
	分析请求，解析出来请求方法，路径地址相对于htdocs目录，协议版本
	文件类型，确定是否为CGI程序
	*/
	char buf[MAXBUFSIZE];
	int numchars;
	size_t i, j;
	if(request==NULL)
	{
		return -1;
	}
	request->client=client;
	numchars=get_line(request->client,buf,MAXBUFSIZE);
	i = 0; j = 0;
	while (!ISspace(buf[j]) && (i < sizeof(request->Method) - 1))
	{
		request->Method[i] = buf[j];
		i++; j++;
	}
	request->Method[i] = '\0';
	
	if (strcasecmp(request->Method, "GET") && strcasecmp(request->Method, "POST"))
	{
		request->ErrorCode=-1;
		request->StaticMsg.StatusCode=404;//Does not support method
		return -2;
	}
	if (strcasecmp(request->Method, "POST") == 0)
	{
		request->ParseState=CGI_FILE;
	}
	i = 0;
	while (ISspace(buf[j]) && (j < sizeof(buf)))
	{
		j++;
	}
	while (!ISspace(buf[j]) && (i < sizeof(request->URL) - 1) && (j < sizeof(buf)))
	{
		request->URL[i] = buf[j];
		i++; j++;
	}
	request->URL[i] = '\0';


	if (strcasecmp(request->Method, "GET") == 0)
	{
		request->Query= request->URL;
		while ((*(request->Query) != '?') && (*(request->Query) != '\0'))
		{
			(request->Query)++;
		}

		if (*(request->Query) == '?')
		{
			request->ParseState= CGI_FILE;
			*(request->Query) = '\0';
			(request->Query)++;
		}
	}
	sprintf(request->Path, "htdocs%s", request->URL);
	if ((request->Path)[strlen(request->Path) - 1] == '/')
	{
		strcat(request->Path, "index.html");
	}

	while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
	{
		numchars = get_line(client, buf, sizeof(buf));
	}

}
int CheckRequest(RESPONSE_MSG *request)
{
	/*
	检查文件访问权限，文件长度，如果是静态文件返回文件描述符
	*/

	struct stat st;
	if (stat(request->Path, &st) == -1) 
	{
		request->ErrorCode=-1;
		request->StaticMsg.StatusCode=404;//The file is not found
	}
	else
	{
		if ((st.st_mode & S_IFMT) == S_IFDIR)
		{
			strcat(request->Path, "/index.html");
			if (stat(request->Path, &st) != -1) 
			{
				request->StaticMsg.ContentLength=st.st_size+2;
			}
			
		}else
		{
			request->StaticMsg.ContentLength=st.st_size+2;
		}
		if ((st.st_mode & S_IXUSR) ||(st.st_mode & S_IXGRP) ||(st.st_mode & S_IXOTH))
		{
			request->ParseState = CGI_FILE;
		}
		
	}
}

int ResponseClient(RESPONSE_MSG *request)
{	
	if(RESPONSE_NO_ERROR(request->ErrorCode))
	{
		if(ISCGI_FILE(request->ParseState))//CGI FILE
		{
			execute_cgi(request->client,request->Path,request->Method,request->Query);
		}else//TEXT FILE
		{
			request->StaticMsg.StatusCode=200;
			ResponseStaticFiles(request,request->Path); 
		}
	}else
	{
		ResponseError(request);
	}
}
int ResponseStaticFiles(RESPONSE_MSG *request,const char *path) 
{
	char buf[MAXBUFSIZE];
	int StatusCode=request->StaticMsg.StatusCode;
	Send_ResponseLineToClient(request->client,StatusCode,Get_ErrorDes(StatusCode));
	Send_ResponseHeadToClient(request->client,"Content-Type",request->StaticMsg.ContentType);
	sprintf(buf,"%ld",request->StaticMsg.ContentLength);
	Send_ResponseHeadToClient(request->client,"Content-Length",buf);
	Send_ResponseHeadToClient(request->client,"Connection","close");
	Send_ResponseHeadToClient(request->client,SERVER_STRING,NULL);
	Send_ResponseBlankLineToClient(request->client);
	Send_ResponseBodyToClient(request->client,path);
}


int ResponseError(RESPONSE_MSG *request) 
{
	char buf[MAXBUFSIZE];
	struct stat st;
	int StatusCode=request->StaticMsg.StatusCode;
	if (stat(Get_ErrorFileFd(StatusCode), &st) != -1) 
	{
		request->StaticMsg.ContentLength=st.st_size+2;
	}
	Send_ResponseLineToClient(request->client,StatusCode,Get_ErrorDes(StatusCode));
	Send_ResponseHeadToClient(request->client,"Content-Type",request->StaticMsg.ContentType);
	sprintf(buf,"%ld",request->StaticMsg.ContentLength);
	Send_ResponseHeadToClient(request->client,"Content-Length",buf);
	Send_ResponseHeadToClient(request->client,"Connection","close");
	Send_ResponseHeadToClient(request->client,SERVER_STRING,NULL);
	Send_ResponseBlankLineToClient(request->client);
	Send_ResponseBodyToClient(request->client,Get_ErrorFileFd(StatusCode));
}

int Send_ResponseLineToClient(int client,int statusCode,const char *des)
{
	char buf[MAXBUFSIZE];
	if(des==NULL)
	{
		return -1;
	}
	sprintf(buf, "HTTP/1.0 %d %s\r\n",statusCode,des);
	send(client, buf, strlen(buf), 0);
	return 0;
}

int Send_ResponseHeadToClient(int client,const char *headName,const char *value)
{
	char buf[MAXBUFSIZE];
	if((headName==NULL))
	{
		return -1;
	}
	if(value ==NULL)
	{
		sprintf(buf, "%s\r\n",headName);
	}else
	{
		sprintf(buf, "%s: %s\r\n",headName,value);
	}
	send(client, buf, strlen(buf), 0);
	return 0;
}

int Send_ResponseBlankLineToClient(int client)
{
	send(client, "\r\n", 2, 0);
	return 0;
}

int Send_ResponseBodyToClient(int client,const char *path)
{
	char buf[MAXBUFSIZE];
	int n=0;
	int fd;
	if((fd=open(path,O_RDONLY))==-1)
	{
		sprintf(buf,"In \"Send_ResponseBodyToClient\"function open %s file failure!",path);
		WriteLogtoFile(501,buf);
		return -1;
	}
	while((n=read(fd,buf,MAXBUFSIZE))!=0)
	{
		send(client, buf, n, 0);
	}
	close(fd);
	return 0;
}

char *Get_ErrorDes(int StatusCode)
{
	int i=0;
	while(i<MAXERRORLISTNUM)
	{
		if(ErrorMap[i]==StatusCode)
		{
			return ErrorDes[i];
		}
		i++;
	}
	return ErrorDes[0];
}
char *Get_ErrorFileFd(int StatusCode)
{
	int i=0;
	while(i<MAXERRORLISTNUM)
	{
		if(ErrorMap[i]==StatusCode)
		{
			return ErrorFile[i];
		}
		i++;
	}
	return ErrorFile[0];
}
int WriteLogtoFile(int errno,const char *msg)
{
	int fd;
	char *buf;
	time_t timer;
	struct tm *ptime;
	buf=(char *)malloc(sizeof(char)*MAXBUFSIZE); //申请文件缓冲区BUFSIZEByte
	if(buf==NULL)
	{
		return -1;
	}	
	timer=time(NULL);
	ptime=localtime(&timer);                //转换为本地时间
    sprintf(buf,"%d-%2d-%2d %2d:%2d:%2d ERROR[%d]:%s \n",(1900+ptime->tm_year),ptime->tm_mon+1,ptime->tm_mday,ptime->tm_hour,ptime->tm_min,ptime->
    tm_sec,errno,msg);
	fd=open(LOGFILE_DIR,O_WRONLY|O_APPEND|O_CREAT,755);
	if(fd==-1)
	{
		return -1;
	}
	write(fd,buf,strlen(buf));
	close(fd);
	free(buf);
	return 0;
}

int main(void)
{
	int server_sock = -1;
	u_short port = 8855;
	int client_sock = -1;
	struct sockaddr_in client_name;
	int client_name_len = sizeof(client_name);
	pthread_t newthread;

	server_sock = Startup(&port);
	printf("httpd running on port %d\n", port);

	while (1)
	{
		client_sock = accept(server_sock,(struct sockaddr *)&client_name,&client_name_len);
		if (client_sock == -1)
		{
			error_die("accept");
		}
		if (pthread_create(&newthread , NULL, Deal_Request, client_sock) != 0)
		{
			perror("pthread_create");
		}
	}

	close(server_sock);

	return(0);
}
