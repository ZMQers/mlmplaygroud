#include <malamute.h>

int main (int argc, char **argv) {

    if (argc < 3) {
        printf("usage: %s tcp://192.168.1.223:9999 myname\n", argv[0]);
        return 1;
    }

    mlm_client_t *client = mlm_client_new ();
    assert(client);

    int rv;
    rv = mlm_client_connect(client, argv[1], 5000, argv[2]);
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
    int count = 0;
    while (!zsys_interrupted) {
        // zclock_sleep(2);
        zmsg_t *msg = zmsg_new();
        assert (msg);
        zmsg_pushstr (msg, "hello");
        //zmsg_print(msg);
        mlm_client_send (client, "testing message", &msg);
        zmsg_destroy (&msg);
        count++;
        if (count % 100 == 0) {
            zsys_info("%i messages sent",count);
        }
        if( count == 10000 ) {
            break;
        }
    }
    mlm_client_destroy(&client);
    zsys_info ("finished.");
    return 0;
}

