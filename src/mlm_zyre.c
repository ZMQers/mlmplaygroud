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

const int s_interval = 1000;

typedef struct  {
    bool terminated;
    bool shout_received;
    bool is_winner;
    zsock_t *pipe;
    zyre_t *zyre_node;
    char *group_name;
} ctrl_block; 

static void
mlm_set_winner(ctrl_block *self, bool winner)
{
    self->is_winner = winner;
    if (self->is_winner) {
        zsys_info("I WON");
        zstr_sendx(self->pipe, "START",  NULL);
    } else {
        zsys_info("I LOST");
        zstr_sendx(self->pipe, "STOP",  NULL);
    }
}

static int
s_mlm_shout_handler(zloop_t *loop, int timer_id, void *arg)
{
    ctrl_block *ctrl = (ctrl_block*)arg;

    if (!ctrl->shout_received && !ctrl->is_winner)
        mlm_set_winner(ctrl, true);

    ctrl->shout_received = false;
    if (ctrl->is_winner) {
        zyre_shouts(ctrl->zyre_node, ctrl->group_name, "tcp://192.168.1.57:9999");
        zsys_info("SHOUT SEND");
    }
    return 0;
}

static int
s_mlm_zyre_handler(zloop_t *loop, zsock_t *reader, void *arg)
{
    ctrl_block *ctrl = (ctrl_block*)arg;
    zmsg_t *msg = zmsg_recv (zyre_socket (ctrl->zyre_node));
    char *event = zmsg_popstr(msg);
    char *peer = zmsg_popstr(msg);
    char *name = zmsg_popstr(msg);
    char *group = zmsg_popstr(msg);
    char *message = zmsg_popstr(msg);

    if (streq(event, "SHOUT")) {
        const char *uuid = zyre_uuid(ctrl->zyre_node);
        size_t uuid_len = strlen(uuid);
        bool i_won = strcmp(uuid, peer) < 0;
        zsys_info("SHOUT from %s, (I win: %d)", peer, i_won);
        if (!i_won)
            ctrl->shout_received = true;
        if (i_won != ctrl->is_winner)
            mlm_set_winner(ctrl, i_won);

    }

    free (event);
    free (peer);
    free (name);
    free (group);
    free (message);
    zmsg_destroy (&msg);
}

static void 
zyre_fn(zsock_t *pipe, void *args)
{
    char *group_name = (char*)args;

    ctrl_block ctrl;
    ctrl.terminated = false;
    ctrl.is_winner = true;
    ctrl.shout_received = true;
    ctrl.group_name = group_name;
    ctrl.pipe = pipe;
    ctrl.zyre_node = zyre_new (NULL);
    if (!ctrl.zyre_node)
        return;                 //  Could not create new node

    zsock_signal (pipe, 0);     //  Signal "ready" to caller

    //zyre_set_verbose (node);  // uncomment to watch the events
    zyre_start(ctrl.zyre_node);
    zsys_info("zyre: Joining group %s as %s", group_name, zyre_uuid(ctrl.zyre_node));
    zyre_join(ctrl.zyre_node, group_name);

    zloop_t *loop = zloop_new();

    zloop_timer(loop, s_interval, 0, s_mlm_shout_handler, &ctrl);

    zloop_reader(loop, zyre_socket(ctrl.zyre_node), s_mlm_zyre_handler, &ctrl);
    zloop_reader_set_tolerant(loop, zyre_socket(ctrl.zyre_node));

    zloop_start(loop);

    zloop_destroy(&loop);
    zyre_stop(ctrl.zyre_node);
    zclock_sleep(100);
    zyre_destroy (&ctrl.zyre_node);
}

static void
broker_node_fn(zsock_t *pipe, void *args)
{
    zpoller_t *poller = zpoller_new(pipe, NULL);
    zactor_t *broker = NULL;

    char *malamute_bind = (char*)args;

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
                    zstr_send(broker, "VERBOSE");
                    zstr_sendx(broker, "BIND", malamute_bind, NULL);
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
    zpoller_destroy(&poller);
}

int 
main (int argc, char *argv[])
{
    if (argc < 3) {
        zsys_info("syntax: ./%s groupname bind");
        zsys_info("where:");
        zsys_info("\tgroupname: zyre group name");
        zsys_info("\tbind: bind value for malamute server");
        zsys_info("e.g. ./%s district9 tcp://192.168.1.20:9999", argv[0], argv[0]);
        exit (0);
    }

    zactor_t *zyre = zactor_new(zyre_fn, argv[1]);
    zactor_t *broker = zactor_new(broker_node_fn, argv[1]);

    assert (zyre);
    assert (zyre_shout);
    assert (broker);

    zstr_sendx(broker, "START", NULL); // startin broker always

    zpoller_t *poller = zpoller_new (zyre, NULL);
    bool terminated = false;
    
    while (!zsys_interrupted) {
        void *which = zpoller_wait (poller, -1); // no timeout
        if (which == zyre) {
            zmsg_t *msg = zmsg_recv (which);
            if (!msg)
                break;              //  Interrupted
            zactor_send(broker, &msg);
        }
    }

    zpoller_destroy(&poller);
    zactor_destroy(&zyre);
    zactor_destroy(&broker);

    return 0;
}


