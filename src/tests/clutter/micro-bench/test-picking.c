
#include <math.h>
#include <stdlib.h>
#include <clutter/clutter.h>

#include "tests/clutter-test-utils.h"

#define N_ACTORS 100
#define N_EVENTS 5

static gint n_actors = N_ACTORS;
static gint n_events = N_EVENTS;

static GOptionEntry entries[] = {
  {
    "num-actors", 'a',
    0,
    G_OPTION_ARG_INT, &n_actors,
    "Number of actors", "ACTORS"
  },
  {
    "num-events", 'e',
    0,
    G_OPTION_ARG_INT, &n_events,
    "Number of events", "EVENTS"
  },
  { NULL }
};

static gboolean
motion_event_cb (ClutterActor *actor, ClutterEvent *event, gpointer user_data)
{
  return FALSE;
}

static void
do_events (ClutterActor *stage)
{
  glong i;
  static gdouble angle = 0;

  for (i = 0; i < n_events; i++)
    {
      angle += (2.0 * G_PI) / (gdouble)n_actors;
      while (angle > G_PI * 2.0)
        angle -= G_PI * 2.0;

      /* If we synthesized events, they would be motion compressed;
       * calling get_actor_at_position() doesn't have that problem
       */
      clutter_stage_get_actor_at_pos (CLUTTER_STAGE (stage),
				      CLUTTER_PICK_REACTIVE,
				      256.0 + 206.0 * cos (angle),
				      256.0 + 206.0 * sin (angle));
    }
}

static void
on_paint (ClutterActor        *stage,
          ClutterPaintContext *paint_context,
          gconstpointer       *data)
{
  do_events (stage);
}

static gboolean
queue_redraw (gpointer stage)
{
  clutter_actor_queue_redraw (CLUTTER_ACTOR (stage));

  return TRUE;
}

int
main (int argc, char **argv)
{
  glong i;
  gdouble angle;
  ClutterColor color = { 0x00, 0x00, 0x00, 0xff };
  ClutterActor *stage, *rect;

  g_setenv ("CLUTTER_VBLANK", "none", FALSE);
  g_setenv ("CLUTTER_DEFAULT_FPS", "1000", FALSE);
  g_setenv ("CLUTTER_SHOW_FPS", "1", FALSE);

  clutter_test_init_with_args (&argc, &argv,
                               NULL,
                               entries,
                               NULL);

  stage = clutter_test_get_stage ();
  clutter_actor_set_size (stage, 512, 512);
  clutter_actor_set_background_color (CLUTTER_ACTOR (stage), CLUTTER_COLOR_Black);
  clutter_stage_set_title (CLUTTER_STAGE (stage), "Picking");

  printf ("Picking performance test with "
          "%d actors and %d events per frame\n",
          n_actors,
          n_events);

  for (i = n_actors - 1; i >= 0; i--)
    {
      angle = ((2.0 * G_PI) / (gdouble) n_actors) * i;

      color.red = (1.0 - ABS ((MAX (0, MIN (n_actors/2.0 + 0, i))) /
                  (gdouble)(n_actors/4.0) - 1.0)) * 255.0;
      color.green = (1.0 - ABS ((MAX (0, MIN (n_actors/2.0 + 0,
                    fmod (i + (n_actors/3.0)*2, n_actors)))) /
                    (gdouble)(n_actors/4) - 1.0)) * 255.0;
      color.blue = (1.0 - ABS ((MAX (0, MIN (n_actors/2.0 + 0,
                   fmod ((i + (n_actors/3.0)), n_actors)))) /
                   (gdouble)(n_actors/4.0) - 1.0)) * 255.0;

      rect = clutter_actor_new ();
      clutter_actor_set_background_color (rect, &color);
      clutter_actor_set_size (rect, 100, 100);
      clutter_actor_set_translation (rect, -50.f, -50.f, 0.f);
      clutter_actor_set_position (rect,
                                  256 + 206 * cos (angle),
                                  256 + 206 * sin (angle));
      clutter_actor_set_reactive (rect, TRUE);
      g_signal_connect (rect, "motion-event",
                        G_CALLBACK (motion_event_cb), NULL);

      clutter_container_add_actor (CLUTTER_CONTAINER (stage), rect);
    }

  clutter_actor_show (stage);

  clutter_threads_add_idle (queue_redraw, stage);

  g_signal_connect (stage, "paint", G_CALLBACK (on_paint), NULL);

  clutter_test_main ();

  clutter_actor_destroy (stage);

  return 0;
}


