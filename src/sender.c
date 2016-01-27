
#include <getopt.h>
#include <malamute.h>

unsigned int num_messages;
unsigned int interval = 1000;
char *endpoint;
char *name;

static struct option longopts[] = {
    { "endpoint",    required_argument,       NULL, 'e' },
    { "num_messages",  required_argument,       NULL, 'm' },
    { "interval", required_argument,       NULL, 'i' },
    { "name", required_argument,       NULL, 'n' },
    { NULL ,    0,         NULL, '\0' }
};

static void usage(void)
{
    fprintf(stderr,
        "Usage: %s -e | --endpoint tcp://1.2.3.4:5\n"
        "       -n | --name name of this instance \n"
        "       [[-i | --interval] sleep between sends (milliseconds), default 1000]\n"
        "       [[-m | --num_messages] number of messages to send, default unlimited]\n",
        "sender");
    exit(1);
}

static void parse_args(int argc, char **argv)
{
    int opt;

    while ((opt = getopt_long(argc, argv, "i:e:n:m:",
                  longopts, 0)) != -1) {
        switch (opt) {
        case 'i':
            interval = atoi(optarg);
            break;
        case 'e':
            endpoint = strdup(optarg);
            break;
        case 'n':
            name = strdup(optarg);
            break;
        case 'm':
            num_messages = atoi(optarg);
            break;

        default:
            fprintf(stderr, "Unknown option -%c\n", opt);
            usage();
        }
    }
}

int main (int argc, char **argv) {
    unsigned int count = 0;

    parse_args(argc, argv);
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
        return -1;
    }

    rv = mlm_client_set_producer (client, "hello-stream");
    if (rv == -1) {
        zsys_error("set_producer failed.");
        mlm_client_destroy (&client);
        return -2;
    }

    while (!zsys_interrupted && (!num_messages || count < num_messages)) {
        zclock_sleep(interval);
        zmsg_t *msg = zmsg_new();
        assert (msg);
        zmsg_pushstr (msg, "hello");
        mlm_client_send (client, "testing message", &msg);
        zmsg_destroy (&msg);
        ++count;
        if (count % 100 == 0) {
            zsys_info ("sent: %u.", count);
        }
    }
    mlm_client_destroy(&client);
    free(endpoint);
    free(name);
    zsys_info ("finished, sent: %u.", count);
    return 0;
}
