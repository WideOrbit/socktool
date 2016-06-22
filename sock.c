#include <sys/types.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <netinet/in.h>
#include <signal.h>
#include <strings.h>
#include <string.h>
#include <errno.h>
#include <netdb.h>

void
check_return_value (int value)
{
  if (value < 0)
    {
      perror (0);
      exit (-1);
    }
}

void
do_server (int argc, char **argv, char blind)
{
  int port;
  char *exe;
  int listenfd;
  int optval;
  struct sockaddr_in serveraddr;	/* server's addr */
  struct sockaddr_in clientaddr;	/* client addr */
  int clientlen;
  struct sigaction sa;
  pid_t exec_child;

  port = atoi (argv[0]);
  exe = argv[1];

  check_return_value (signal (SIGCHLD, SIG_IGN) == SIG_ERR ? -1 : 0);

  check_return_value (listenfd = socket (AF_INET, SOCK_STREAM, 0));

  optval = 1;
  setsockopt (listenfd, SOL_SOCKET, SO_REUSEADDR, (const void *) &optval,
	      sizeof (int));

  bzero ((char *) &serveraddr, sizeof (serveraddr));
  serveraddr.sin_family = AF_INET;
  serveraddr.sin_addr.s_addr = htonl (INADDR_ANY);
  serveraddr.sin_port = htons ((unsigned short) port);
  check_return_value (bind
		      (listenfd, (struct sockaddr *) &serveraddr,
		       sizeof (serveraddr)));

  check_return_value (listen (listenfd, 5));

  clientlen = sizeof (clientaddr);

  for (;;)
    {
      pid_t pid;
      int childfd =
	accept (listenfd, (struct sockaddr *) &clientaddr, &clientlen);
      check_return_value (childfd);

      pid = fork ();

      if (pid == 0)
	{
	  int child_stdin[2];
	  int child_stdout[2];
	  pipe (child_stdin);
	  pipe (child_stdout);

	  exec_child = fork ();

	  if (exec_child == 0)
	    {
	      close (childfd);
	      close (child_stdin[1]);
	      close (child_stdout[0]);
	      dup2 (child_stdin[0], STDIN_FILENO);
	      close (child_stdin[0]);
	      dup2 (child_stdout[1], STDOUT_FILENO);
	      close (child_stdout[1]);
	      check_return_value (execl (exe, exe, NULL));
	    }
	  else
	    {
	      /* wait for input on pipes[1] or childfd */
	      /* if sigpipe on childfd, kill child process */
	      char read_childfd;
	      char read_child_stdout;
	      close (child_stdin[0]);
	      close (child_stdout[1]);
	      signal (SIGPIPE, SIG_IGN);

	      read_childfd = !blind;
	      read_child_stdout = 1;

	      while (read_childfd || read_child_stdout)
		{
		  int n;
		  char buf[1024];
		  fd_set fds;
		  int select_r;
		  int max_fd = -1;
		  FD_ZERO (&fds);
		  if (read_childfd)
		    {
		      max_fd = childfd > max_fd ? childfd : max_fd;
		      FD_SET (childfd, &fds);
		    }

		  if (read_child_stdout)
		    {
		      max_fd =
			child_stdout[0] > max_fd ? child_stdout[0] : max_fd;
		      FD_SET (child_stdout[0], &fds);
		    }

		  select_r = select (max_fd + 1, &fds, NULL, NULL, NULL);
		  if (select_r == -1)
		    {
		      if (errno == EINTR)
			continue;
		      else
			break;
		    }
		  if (read_childfd && FD_ISSET (childfd, &fds))
		    {
		      /* write from childfd to child_stdin[1] */
		      int r = read (childfd, buf, sizeof (buf));
		      if (r <= 0)
			{
			  close (child_stdin[1]);
			  read_childfd = 0;
			}
		      else
			{
			  write (child_stdin[1], buf, r);
			}
		    }
		  if (read_child_stdout && FD_ISSET (child_stdout[0], &fds))
		    {
		      /* write from child_stdout[0] to childfd */
		      int r = read (child_stdout[0], buf, sizeof (buf));
		      if (r <= 0)
			{
			  shutdown (childfd, SHUT_WR);
			  read_child_stdout = 0;
			}
		      else
			{
			  r = write (childfd, buf, r);
			  if (r < 0)
			    {
			      /* peer disconnected */
			      break;
			    }
			}
		    }
		}

	      if (exec_child != -1)
		{
		  kill (exec_child, SIGHUP);
		}

	      close (childfd);
	    }

	  return;
	}
      else
	{
	  close (childfd);
	}
    }

  return;
}

void
do_connect (int argc, char **argv, char connectwait)
{
  int fd, portno, n;
  struct sockaddr_in serv_addr;
  struct hostent *server;
  char read_stdin;
  char read_fd;
  char stdin_closed;
  char received_anything;

  if (argc < 2)
    {
      fprintf (stderr, "invalid number of arguments\n");
      exit (0);
    }
  portno = atoi (argv[1]);
  check_return_value (fd = socket (AF_INET, SOCK_STREAM, 0));

  server = gethostbyname (argv[0]);
  if (server == NULL)
    {
      fprintf (stderr, "gethostbyname failed\n");
      exit (0);
    }

  argc -= 2;
  argv += 2;

  bzero ((char *) &serv_addr, sizeof (serv_addr));
  serv_addr.sin_family = AF_INET;
  bcopy ((char *) server->h_addr, (char *) &serv_addr.sin_addr.s_addr,
	 server->h_length);
  serv_addr.sin_port = htons (portno);
  check_return_value (connect
		      (fd, (struct sockaddr *) &serv_addr,
		       sizeof (serv_addr)));
  signal (SIGPIPE, SIG_IGN);

  read_stdin = 1;
  read_fd = 1;
  stdin_closed = 0;
  received_anything = 0;
  while (argc > 0)
    {
      int r = write (fd, argv[0], strlen (argv[0]));
      if (r < 0)
	{
	  return;
	}
      r = write (fd, "\n", 1);
      if (r < 0)
	{
	  return;
	}
      --argc;
      ++argv;
    }

  while (read_stdin || read_fd)
    {
      int n;
      char buf[1024];
      fd_set fds;
      int select_r;
      FD_ZERO (&fds);
      if (read_stdin)
	{
	  FD_SET (STDIN_FILENO, &fds);
	}

      if (read_fd)
	{
	  FD_SET (fd, &fds);
	}

      select_r = select (fd + 1, &fds, NULL, NULL, NULL);
      if (select_r == -1)
	{
	  if (errno == EINTR)
	    continue;
	  else
	    {
	      perror (0);
	      break;
	    }
	}
      if (read_stdin && FD_ISSET (STDIN_FILENO, &fds))
	{
	  /* write from childfd to child_stdin[1] */
	  int r = read (STDIN_FILENO, buf, sizeof (buf));
	  if (r <= 0)
	    {
	      if (received_anything && !connectwait)
		{
		  shutdown (fd, SHUT_WR);
		}
	      else
		{
		  stdin_closed = 1;
		}
	      /*close(fd); */
	      read_stdin = 0;
	    }
	  else
	    {
	      r = write (fd, buf, r);
	      if (r < 0)
		{
		  /* peer disconnected */
		  break;
		}
	    }
	}
      if (read_fd && FD_ISSET (fd, &fds))
	{
	  /* write from child_stdout[0] to childfd */
	  int r = read (fd, buf, sizeof (buf));
	  if (r <= 0)
	    {
	      read_fd = 0;

	      if (connectwait)
		{
		  read_stdin = 0;
		}
	    }
	  else
	    {
	      received_anything = 1;
	      if (stdin_closed && !connectwait)
		{
		  shutdown (fd, SHUT_WR);
		  stdin_closed = 0;
		}
	      r = write (STDOUT_FILENO, buf, r);
	      if (r < 0)
		{
		  /* peer disconnected */
		  break;
		}
	    }
	}
    }

  close (fd);
}

int
main (int argc, char **argv)
{
  if (!strcmp (argv[1], "listen"))
    {
      do_server (argc - 2, argv + 2, 0);
    }
  else if (!strcmp (argv[1], "blindlisten"))
    {
      do_server (argc - 2, argv + 2, 1);
    }
  else if (!strcmp (argv[1], "connect"))
    {
      do_connect (argc - 2, argv + 2, 0);
    }
  else if (!strcmp (argv[1], "connectwait"))
    {
      do_connect (argc - 2, argv + 2, 1);
    }
}
