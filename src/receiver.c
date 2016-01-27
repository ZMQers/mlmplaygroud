#include <malamute.h>
#include <getopt.h>

char *endpoint;
char *name;

static struct option longopts[] = {
    { "endpoint",    required_argument,       NULL, 'e' },
    { "name", required_argument,       NULL, 'n' },
    { NULL ,    0,         NULL, '\0' }
};

static void usage(void)
{
    fprintf(stderr,
        "Usage: %s -e | --endpoint tcp://1.2.3.4:5\n"
        "       -n | --name name of this instance \n"
        "receiver");
    exit(1);
}

static void parse_args(int argc, char **argv)
{
    int opt;

    while ((opt = getopt_long(argc, argv, "i:e:n:m:",
                  longopts, 0)) != -1) {
        switch (opt) {
        case 'e':
            endpoint = strdup(optarg);
            break;
        case 'n':
            name = strdup(optarg);
            break;
        default:
            fprintf(stderr, "Unknown option -%c\n", opt);
            usage();
        }
    }
}


int main (int argc, char **argv) {

    parse_args(argc,argv);
    if (!endpoint || !name) {
        zsys_error("endpoint or name not specified.");
        usage();
    }

    mlm_client_t *client = mlm_client_new ();
    assert(client);

    int rv;
    rv = mlm_client_connect(client, endpoint, 5000, name);
    if (rv == -1) {
        zsys_error("connection failed.");
        mlm_client_destroy (&client);
        return 1;
    }

    /* I don't produce any messages
    rv = mlm_client_set_producer (client, "hello-stream");
    if (rv != 0) {
        zsys_error("set_producer failed.");
        mlm_client_destroy (&client);
        return -2;
    }
    */

    rv = mlm_client_set_consumer (client, "hello-stream", ".*");
    if (rv == -1) {
        zsys_error ("set_consumer failed.");
        mlm_client_destroy (&client);
        exit(1);
    }
    int count = 0;
    while (!zsys_interrupted) {
        zmsg_t *msg = mlm_client_recv (client);
        if (msg) {
            zsys_info ("sender: %s", mlm_client_sender(client));
            zmsg_destroy (&msg);
            count++;
            if (count % 100 == 0) zsys_info("%i messages received",count);
        }
    }
    mlm_client_destroy(&client);
    zsys_info ("finished.");
    return 0;
}

