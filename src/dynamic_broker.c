//  --------------------------------------------------------------------------
//  Example Zyre distributed chat application
//
//  --------------------------------------------------------------------------
//  Copyright (c) 2010-2014 iMatix Corporation and Contributors
//
//  Permission is hereby granted, free of charge, to any person obtaining a
//  copy of this software and associated documentation files (the "Software"),
//  to deal in the Software without restriction, including without limitation
//  the rights to use, copy, modify, merge, publish, distribute, sublicense,
//  and/or sell copies of the Software, and to permit persons to whom the
//  Software is furnished to do so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in
//  all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
//  THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
//  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
//  DEALINGS IN THE SOFTWARE.
//  --------------------------------------------------------------------------


#include "zyre.h"
#include "malamute.h"

static void
zyre_actor (zsock_t *pipe, void *args)
{
    zactor_t *server = NULL;
    //  Do some initialization
    char   *name = ((char**)args)[0];
    char   *group = ((char**)args)[1];
    char   *endpoint = ((char**)args)[2];
    int     timeout = atoi(((char**)args)[3]);

    zyre_t *node = zyre_new (name);
    if (!node)
        return;                 //  Could not create new node
    //zyre_set_verbose (node);  // uncomment to watch the events
    zyre_start (node);
    zyre_join (node, group);
    zsock_signal (pipe, 0);     //  Signal "ready" to caller

    bool terminated = false;
    zpoller_t *poller = zpoller_new (pipe, zyre_socket (node), NULL);
    bool found_broker = false;
    while (!terminated) {
        int64_t time_start = zclock_mono();
        void *which = zpoller_wait (poller, 1000);
        if (which == pipe) {
            zmsg_t *msg = zmsg_recv (which);
            if (!msg)
                break;              //  Interrupted
            char *command = zmsg_popstr (msg);
            if (streq (command, "$TERM")) {
                terminated = true;
                puts("Received TERM");
                if (server) {
                    puts("destroying server");
                    zactor_destroy (&server);
                    server = NULL;
                }
            }
            else
            if (streq (command, "SHOUT")) {
                char *string = zmsg_popstr (msg);
                zyre_shouts (node, group, "%s", string);
	    }
            else {
                puts ("E: invalid message to actor");
                assert (false);
            }
            free (command);
            zmsg_destroy (&msg);
        }
        else if (which == zyre_socket (node)) {
            zmsg_t *msg = zmsg_recv (which);
            char *event = zmsg_popstr (msg);
            char *peer = zmsg_popstr (msg);
            char *name = zmsg_popstr (msg);
            char *group = zmsg_popstr (msg);
            char *message = zmsg_popstr (msg);

            if (streq (event, "ENTER")) {
                printf ("%s has joined the group\n", name);
            }
            else if (streq (event, "EXIT")) {
                printf ("%s has left the group\n", name);
            }
            else if (streq (event, "SHOUT")) {
                const char* uuid = zyre_uuid(node);
                printf ("received SHOUT from %s: %s\n", name, message);
                // printf ("comparing my UUID %s and peer %s\n", uuid, peer);
                if (strcmp(uuid, peer) > 0) {
                   // broker with lower ID found
                   found_broker = true;
                   puts("I should (be) stop(ped) now");
                   if (server) {
                       puts("destroying server");
                       zactor_destroy (&server);
                       server = NULL;
                   }
                } else {
                    puts("ignoring");
                }
            }
	    else if (streq (event, "EVASIVE")) {
	        printf ("%s is being evasive\n", name);
	    }
            //printf ("Message from node\n");
            //printf ("event: %s peer: %s  name: %s\n  group: %s message: %s\n", event, peer, name, group, message);

            free (event);
            free (peer);
            free (name);
            free (group);
            free (message);
            zmsg_destroy (&msg);
        }
        int64_t time_end = zclock_mono();
        bool shout_timeout = (time_end - time_start) > timeout;
        if(shout_timeout && !terminated) {
            if (!found_broker) {

                zyre_shouts (node, group, "%s", endpoint);
                puts("I should be running now");
                if (server == NULL) {
                    puts("creating new server");
                    server = zactor_new (mlm_server, "Malamute");
                    if(server) {
                        zstr_send(server, "VERBOSE");
                        zstr_sendx(server, "BIND", endpoint, NULL);
                    } else
                        puts("E: server not created!");
                }
            }
            found_broker = false;
        }
    }
    zpoller_destroy (&poller);

    // Notify peers that this peer is shutting down. Provide
    // a brief interval to ensure message is emitted.
    zyre_stop(node);
    zclock_sleep(100);

    zyre_destroy (&node);
}

int
main (int argc, char *argv[])
{
    if (argc < 4) {
        printf ("syntax: %s myname group timeout", argv[0]);
        exit (0);
    }
    zactor_t *zyreactor = zactor_new (zyre_actor, &argv[1]);
    assert(zyreactor);
    while (!zsys_interrupted) {
        zclock_sleep(100);
    }

    zactor_destroy (&zyreactor);

    return 0;
}


