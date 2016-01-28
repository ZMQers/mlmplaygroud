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
zyre_shout_fn(zsock_t *pipe, void *args)
{
    zactor_t *z_node = (zactor_t*)(args);
    zpoller_t *poller = zpoller_new(pipe, NULL);
    zsock_signal (pipe, 0);     //  Signal "ready" to caller

    bool terminated = false;
    bool shout = true;
    while(!terminated) {

        void *which = NULL;

        if (shout) {

            which = zpoller_wait (poller, 0); // no timeout
            terminated = zpoller_terminated(poller);

        } else {
            which = zpoller_wait (poller, -1); // no timeout
        }

        if (which == pipe) {
            zmsg_t *msg = zmsg_recv(which);
            char *command = zmsg_popstr(msg);
            
            if (streq (command, "$TERM")) {
                terminated = true;
            }
            else
            if (streq(command, "STOP")) {
                zsys_info("SHOUT: STOP");
                shout = false;
            }
            else 
            if (streq(command, "START")) {
                zsys_info("SHOUT: START");
                shout = true;
            }
            free(command);
            zmsg_destroy(&msg);
        }

        if (!terminated) {
            if (shout) {
                zstr_sendx(z_node, "SHOUT", "I am the winner :)", NULL);
                zclock_sleep(1000);
            } else {
            }
        }

    }
    zpoller_destroy(&poller);
}

static void 
zyre_fn(zsock_t *pipe, void *args)
{
    char *group_name = (char*)args;
    zyre_t *node = zyre_new (NULL);
    if (!node)
        return;                 //  Could not create new node

    //zyre_set_verbose (node);  // uncomment to watch the events
    zyre_start(node);

    zsys_info("zyre: Joining group %s as %s", group_name, zyre_uuid(node));
    zyre_join(node, group_name);

    zsock_signal (pipe, 0);     //  Signal "ready" to caller

    bool terminated = false;
    bool winner = true;
    zpoller_t *poller = zpoller_new (pipe, zyre_socket (node), NULL);
    while (!terminated) {
        void *which = zpoller_wait (poller, -1); // no timeout
        if (which == pipe) {
            zmsg_t *msg = zmsg_recv (which);
            if (!msg)
                break;              //  Interrupted
            char *command = zmsg_popstr (msg);
            if (streq (command, "$TERM")) {
                terminated = true;
            }
            else
            if (streq(command, "SHOUT")) { 
                char *string = zmsg_popstr(msg);
                zyre_shouts(node, group_name, "%s", string);
                zsys_info("SHOUT SEND");
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

            if (streq(event, "SHOUT")) {
                const char *uuid = zyre_uuid(node);
                size_t uuid_len = strlen(uuid);
                bool i_won = strcmp(uuid, peer) < 0;
                if (i_won != winner) {
                    winner = i_won;
                    if (winner) {
                        zstr_sendx(pipe, "START",  NULL);
                    } else {
                        zstr_sendx(pipe, "STOP",  NULL);
                    }

                }
            }

            free (event);
            free (peer);
            free (name);
            free (group);
            free (message);
            zmsg_destroy (&msg);
        }
    }
    zpoller_destroy (&poller);

    // Notify peers that this peer is shutting down. Provide
    // a brief interval to ensure message is emitted.
    zyre_stop(node);
    zclock_sleep(100);
    zyre_destroy (&node);
}

static void
broker_node_fn(zsock_t *pipe, void *args)
{
    zpoller_t *poller = zpoller_new(pipe, NULL);
    zactor_t *broker = NULL;

    zsock_signal (pipe, 0);     //  Signal "ready" to caller
    
    int terminated = false;

    while (!terminated) {

        void *which = zpoller_wait (poller, -1); // no timeout

        if (which == pipe) {
            zmsg_t *msg = zmsg_recv (which);
            if (!msg)
                break;              //  Interrupted
            char *command = zmsg_popstr (msg);

            if (streq(command, "$TERM")) {
                terminated = true;
            }
            else
            if (streq (command, "START")) { 
                if (!broker) {
                    broker = zactor_new(mlm_server, NULL);
                    zsys_info("BROKER START");
                }
            }
            else
            if (streq (command, "STOP")) { 
                assert(broker);
                zactor_destroy(&broker);
                broker = NULL;
                zsys_info("BROKER STOP");
            }
            free(command);
            zmsg_destroy(&msg);
        }
    }

    if (broker)
        zactor_destroy(&broker);
}

int 
main (int argc, char *argv[])
{
    if (argc < 2) {
        puts ("syntax: ./chat group");
        exit (0);
    }

    zactor_t *zyre = zactor_new(zyre_fn, argv[1]);
    zactor_t *broker = zactor_new(broker_node_fn, argv[1]);
    zactor_t *zyre_shout = zactor_new(zyre_shout_fn, (void*)zyre);

    assert (zyre);
    assert (zyre_shout);
    assert (broker);

    zsys_info("%s", "Starting broker...!");
    zstr_sendx(broker, "START", NULL);

    zpoller_t *poller = zpoller_new (zyre, NULL);
    bool terminated = false;
    
    while (!zsys_interrupted) {
        void *which = zpoller_wait (poller, -1); // no timeout
        if (which == zyre) {
            zmsg_t *msg = zmsg_recv (which);
            if (!msg)
                break;              //  Interrupted
            zmsg_t *zyre_msg = zmsg_dup(msg);
            zactor_send(zyre_shout, &zyre_msg);
            zactor_send(broker, &msg);
        }
    }

    zactor_destroy(&zyre);
    zactor_destroy(&zyre_shout);
    zactor_destroy(&broker);

    return 0;
}


