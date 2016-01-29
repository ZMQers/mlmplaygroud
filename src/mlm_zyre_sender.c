#include "zyre.h"
#include "malamute.h"

const int s_interval = 1000;

typedef struct  {
    mlm_client_t *mlm_client;
    char *endpoint;
    zyre_t *zyre_node;
} sender_t;

static int
client_connect(sender_t *self)
{
    if (!self->mlm_client)
        mlm_client_destroy(&self->mlm_client);
 
    if (!self->endpoint) {
        zsys_error("no broker available.");
        return -1;
    }

    self->mlm_client = mlm_client_new();
        
    int rv = mlm_client_connect(self->mlm_client, self->endpoint, 5000, "kladku-producer");
    if (rv == -1) {
        zsys_error("connection failed.");
        mlm_client_destroy(&self->mlm_client);
        return -1;
    }

    zsys_info("client connected");

    rv = mlm_client_set_producer (self->mlm_client, "hello-stream");
    if (rv == -1) {
        zsys_error("set_producer failed.");
        mlm_client_destroy (&self->mlm_client);
        return -2;
    }


}

static int
s_client_zyre_handler(zloop_t *loop, zsock_t *reader, void *arg)
{
    sender_t *self = (sender_t*)arg;
    zmsg_t *msg = zmsg_recv(zyre_socket(self->zyre_node));
    char *event = zmsg_popstr(msg);
    char *peer = zmsg_popstr(msg);
    char *name = zmsg_popstr(msg);
    char *group = zmsg_popstr(msg);
    char *message = zmsg_popstr(msg);

    if (streq(event, "SHOUT")) {
        zsys_info("SHOUT %s %s %s %s", peer, name, group, message);
        if (!self->endpoint || strcmp(message, self->endpoint) != 0) {
            char *tmp = self->endpoint;
            self->endpoint = message;
            message = tmp;
            client_connect(self);
        }
    }

    free(event);
    free(peer);
    free(name);
    free(group);
    if (message)
        free(message);
    zmsg_destroy(&msg);
}

static int
s_client_sender_handler(zloop_t *loop, int timer, void *arg)
{
    sender_t *self = (sender_t*)arg;
    if (self->mlm_client) {
        zmsg_t *msg = zmsg_new();
        assert (msg);
        zmsg_pushstr (msg, "hello");
        mlm_client_send (self->mlm_client, "testing message", &msg);
        zmsg_destroy (&msg);
    }
    return 0;
}

int 
main (int argc, char *argv[])
{
    if (argc < 2) {
        zsys_info("syntax: %s group", argv[0]);
        exit (0);
    }

    sender_t self;
    self.mlm_client = NULL;
    self.endpoint = NULL;
    self.zyre_node = zyre_new (NULL);

    char *groupname = (char*)argv[1];

    zyre_start(self.zyre_node);
    zsys_info("zyre: Joining group %s as %s", groupname, zyre_uuid(self.zyre_node));
    zyre_join(self.zyre_node, groupname);

    zloop_t *loop = zloop_new();

    zloop_reader(loop, zyre_socket(self.zyre_node), s_client_zyre_handler, &self);
    zloop_timer(loop, s_interval, 0, s_client_sender_handler, &self);

    zloop_start(loop);

    zloop_destroy(&loop);
    zyre_stop(self.zyre_node);
    zclock_sleep(100);
    zyre_destroy (&self.zyre_node);

    if (self.mlm_client)
        mlm_client_destroy(&self.mlm_client);
    if (self.endpoint)
        free(self.endpoint);

    return 0;
}


