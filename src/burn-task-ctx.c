/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/*
 * brasero
 * Copyright (C) Philippe Rouquier 2007 <bonfire-app@wanadoo.fr>
 * 
 * brasero is free software.
 * 
 * You may redistribute it and/or modify it under the terms of the
 * GNU General Public License, as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option)
 * any later version.
 * 
 * brasero is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with brasero.  If not, write to:
 * 	The Free Software Foundation, Inc.,
 * 	51 Franklin Street, Fifth Floor
 * 	Boston, MA  02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>
#include <glib-object.h>
#include <glib/gi18n-lib.h>

#include "burn-basics.h"
#include "burn-session.h"
#include "burn-debug.h"
#include "burn-task-ctx.h"

typedef struct _BraseroTaskCtxPrivate BraseroTaskCtxPrivate;
struct _BraseroTaskCtxPrivate
{
	/* these two are set at creation time and can't be changed */
	BraseroTaskAction action;
	BraseroBurnSession *session;

	GMutex *lock;

	BraseroTrack *current_track;
	GSList *tracks;

	/* used to poll for progress (every 0.5 sec) */
	gdouble progress;
	gint64 track_bytes;
	gint64 session_bytes;

	gint64 size;
	gint64 blocks;

	/* keep track of time */
	GTimer *timer;
	gint64 first_written;

	/* used for immediate rate */
	gint64 current_written;
	gdouble current_elapsed;
	gint64 last_written;
	gdouble last_elapsed;

	/* used for remaining time */
	GSList *times;
	gdouble total_time;

	/* used for rates that certain jobs are able to report */
	gint64 rate;

	/* the current action */
	/* FIXME: we need two types of actions */
	BraseroBurnAction current_action;
	gchar *action_string;

	guint dangerous;

	guint fake:1;
	guint action_changed:1;
	guint written_changed:1;
	guint progress_changed:1;
	guint use_average_rate:1;
};

#define BRASERO_TASK_CTX_PRIVATE(o)  (G_TYPE_INSTANCE_GET_PRIVATE ((o), BRASERO_TYPE_TASK_CTX, BraseroTaskCtxPrivate))

G_DEFINE_TYPE (BraseroTaskCtx, brasero_task_ctx, G_TYPE_OBJECT);

#define MAX_VALUE_AVERAGE	16

enum _BraseroTaskCtxSignalType {
	ACTION_CHANGED_SIGNAL,
	PROGRESS_CHANGED_SIGNAL,
	LAST_SIGNAL
};
static guint brasero_task_ctx_signals [LAST_SIGNAL] = { 0 };

enum
{
	PROP_0,
	PROP_ACTION,
	PROP_SESSION
};

static GObjectClass* parent_class = NULL;

void
brasero_task_ctx_set_dangerous (BraseroTaskCtx *self, gboolean value)
{
	BraseroTaskCtxPrivate *priv;

	priv = BRASERO_TASK_CTX_PRIVATE (self);
	if (value)
		priv->dangerous ++;
	else
		priv->dangerous --;
}

guint
brasero_task_ctx_get_dangerous (BraseroTaskCtx *self)
{
	BraseroTaskCtxPrivate *priv;

	priv = BRASERO_TASK_CTX_PRIVATE (self);
	return priv->dangerous;
}

void
brasero_task_ctx_reset (BraseroTaskCtx *self)
{
	BraseroTaskCtxPrivate *priv;
	GSList *tracks;

	priv = BRASERO_TASK_CTX_PRIVATE (self);

	if (priv->tracks) {
		g_slist_foreach (priv->tracks, (GFunc) brasero_track_unref, NULL);
		g_slist_free (priv->tracks);
		priv->tracks = NULL;
	}

	tracks = brasero_burn_session_get_tracks (priv->session);
	BRASERO_BURN_LOG ("Setting current track (%i tracks)", g_slist_length (tracks));
	if (priv->current_track)
		brasero_track_unref (priv->current_track);

	if (tracks) {
		priv->current_track = tracks->data;
		brasero_track_ref (priv->current_track);
	}
	else
		BRASERO_BURN_LOG ("no tracks");

	if (priv->timer) {
		g_timer_destroy (priv->timer);
		priv->timer = NULL;
	}

	priv->dangerous = 0;
	priv->progress = -1.0;
	priv->track_bytes = -1;
	priv->session_bytes = -1;
	priv->written_changed = 0;
	priv->current_written = 0;
	priv->current_elapsed = 0;
	priv->last_written = 0;
	priv->last_elapsed = 0;

	if (priv->times) {
		g_slist_free (priv->times);
		priv->times = NULL;
	}

	g_signal_emit (self,
		       brasero_task_ctx_signals [PROGRESS_CHANGED_SIGNAL],
		       0);
}

void
brasero_task_ctx_set_fake (BraseroTaskCtx *ctx,
			   gboolean fake)
{
	BraseroTaskCtxPrivate *priv;

	priv = BRASERO_TASK_CTX_PRIVATE (ctx);
	priv->fake = fake;
}

/**
 * Used to get config
 */

BraseroBurnSession *
brasero_task_ctx_get_session (BraseroTaskCtx *self)
{
	BraseroTaskCtxPrivate *priv;

	priv = BRASERO_TASK_CTX_PRIVATE (self);
	if (!priv->session)
		return NULL;

	return priv->session;
}

BraseroBurnResult
brasero_task_ctx_get_stored_tracks (BraseroTaskCtx *self,
				    GSList **tracks)
{
	BraseroTaskCtxPrivate *priv;

	g_return_val_if_fail (tracks != NULL, BRASERO_BURN_ERR);

	priv = BRASERO_TASK_CTX_PRIVATE (self);
	if (!priv->current_track)
		return BRASERO_BURN_ERR;

	*tracks = priv->tracks;
	return BRASERO_BURN_OK;
}

BraseroBurnResult
brasero_task_ctx_get_current_track (BraseroTaskCtx *self,
				    BraseroTrack **track)
{
	BraseroTaskCtxPrivate *priv;

	g_return_val_if_fail (track != NULL, BRASERO_BURN_ERR);

	priv = BRASERO_TASK_CTX_PRIVATE (self);
	if (!priv->current_track)
		return BRASERO_BURN_ERR;

	*track = priv->current_track;
	return BRASERO_BURN_OK;
}

BraseroTaskAction
brasero_task_ctx_get_action (BraseroTaskCtx *self)
{
	BraseroTaskCtxPrivate *priv;

	priv = BRASERO_TASK_CTX_PRIVATE (self);

	if (priv->fake)
		return BRASERO_TASK_ACTION_NONE;

	return priv->action;
}

/**
 * Used to report task status
 */

BraseroBurnResult
brasero_task_ctx_add_track (BraseroTaskCtx *self,
			    BraseroTrack *track)
{
	BraseroTaskCtxPrivate *priv;

	priv = BRASERO_TASK_CTX_PRIVATE (self);

	BRASERO_BURN_LOG ("Adding track (type = %i) %s",
			  brasero_track_get_type (track, NULL),
			  priv->tracks? "already some tracks":"");
	
	priv->tracks = g_slist_prepend (priv->tracks, track);
	return BRASERO_BURN_OK;
}

static gboolean
brasero_task_ctx_set_next_track (BraseroTaskCtx *self)
{
	BraseroTaskCtxPrivate *priv;
	GSList *tracks;
	GSList *node;

	priv = BRASERO_TASK_CTX_PRIVATE (self);

	/* we need to set the next track if our action is NORMAL or CHECKSUM */
	if (priv->action != BRASERO_TASK_ACTION_NORMAL
	&&  priv->action != BRASERO_TASK_ACTION_CHECKSUM)
		return BRASERO_BURN_OK;

	/* see if there is another track left */
	tracks = brasero_burn_session_get_tracks (priv->session);
	node = g_slist_find (tracks, priv->current_track);
	if (!node || !node->next)
		return BRASERO_BURN_OK;

	priv->session_bytes += priv->track_bytes;
	priv->track_bytes = 0;

	if (priv->current_track)
		brasero_track_unref (priv->current_track);

	priv->current_track = node->next->data;
	brasero_track_ref (priv->current_track);

	return BRASERO_BURN_RETRY;
}

BraseroBurnResult
brasero_task_ctx_next_track (BraseroTaskCtx *self)

{
	BraseroTaskCtxPrivate *priv;
	BraseroBurnResult retval;

	g_return_val_if_fail (BRASERO_IS_TASK_CTX (self), BRASERO_BURN_ERR);

	priv = BRASERO_TASK_CTX_PRIVATE (self);

	retval = brasero_task_ctx_set_next_track (self);
	if (retval == BRASERO_BURN_RETRY) {
		BraseroTaskCtxClass *klass;

		BRASERO_BURN_LOG ("Set next track to be processed");

		klass = BRASERO_TASK_CTX_GET_CLASS (self);
		if (!klass->finished)
			return BRASERO_BURN_NOT_SUPPORTED;

		klass->finished (self,
				 BRASERO_BURN_RETRY,
				 NULL);
		return BRASERO_BURN_RETRY;
	}

	BRASERO_BURN_LOG ("No next track to process");
	return BRASERO_BURN_OK;
}

BraseroBurnResult
brasero_task_ctx_finished (BraseroTaskCtx *self)
{
	BraseroTaskCtxPrivate *priv;
	BraseroTaskCtxClass *klass;
	GError *error = NULL;
	GSList *iter;

	priv = BRASERO_TASK_CTX_PRIVATE (self);
	klass = BRASERO_TASK_CTX_GET_CLASS (self);
	if (!klass->finished)
		return BRASERO_BURN_NOT_SUPPORTED;

	klass->finished (self,
			 BRASERO_BURN_OK,
			 error);

	if (priv->tracks) {
		brasero_burn_session_push_tracks (priv->session);
		priv->tracks = g_slist_reverse (priv->tracks);
		for (iter = priv->tracks; iter; iter = iter->next) {
			BraseroTrack *track;

			track = iter->data;
			brasero_burn_session_add_track (priv->session, track);
		}
	
		g_slist_free (priv->tracks);
		priv->tracks = NULL;
	}

	return BRASERO_BURN_OK;
}

BraseroBurnResult
brasero_task_ctx_error (BraseroTaskCtx *self,
			BraseroBurnResult retval,
			GError *error)
{
	BraseroTaskCtxClass *klass;
	BraseroTaskCtxPrivate *priv;

	priv = BRASERO_TASK_CTX_PRIVATE (self);
	klass = BRASERO_TASK_CTX_GET_CLASS (self);
	if (!klass->finished)
		return BRASERO_BURN_NOT_SUPPORTED;

	klass->finished (self,
			 retval,
			 error);

	return BRASERO_BURN_OK;
}

BraseroBurnResult
brasero_task_ctx_start_progress (BraseroTaskCtx *self,
				 gboolean force)

{
	BraseroTaskCtxPrivate *priv;

	g_return_val_if_fail (BRASERO_IS_TASK_CTX (self), BRASERO_BURN_ERR);

	priv = BRASERO_TASK_CTX_PRIVATE (self);

	if (!priv->timer) {
		priv->timer = g_timer_new ();
		priv->first_written = priv->session_bytes + priv->track_bytes;
	}
	else if (force) {
		g_timer_start (priv->timer);
		priv->first_written = priv->session_bytes + priv->track_bytes;
	}

	return BRASERO_BURN_OK;
}

static gdouble
brasero_task_ctx_get_average (GSList **values, gdouble value)
{
	const unsigned int scale = 10000;
	unsigned int num = 0;
	gdouble average;
	gint32 int_value;
	GSList *l;

	if (value * scale < G_MAXINT)
		int_value = (gint32) ceil (scale * value);
	else if (value / scale < G_MAXINT)
		int_value = (gint32) ceil (-1.0 * value / scale);
	else
		return value;
		
	*values = g_slist_prepend (*values, GINT_TO_POINTER (int_value));

	average = 0;
	for (l = *values; l; l = l->next) {
		gdouble r = (gdouble) GPOINTER_TO_INT (l->data);

		if (r < 0)
			r *= scale * -1.0;
		else
			r /= scale;

		average += r;
		num++;
		if (num == MAX_VALUE_AVERAGE && l->next)
			l = g_slist_delete_link (l, l->next);
	}

	average /= num;
	return average;
}

void
brasero_task_ctx_report_progress (BraseroTaskCtx *self)
{
	BraseroTaskCtxPrivate *priv;
	gdouble progress, elapsed;

	priv = BRASERO_TASK_CTX_PRIVATE (self);

	if (priv->action_changed) {
		g_signal_emit (self,
			       brasero_task_ctx_signals [ACTION_CHANGED_SIGNAL],
			       0,
			       priv->current_action);
		priv->action_changed = 0;
	}

	if (priv->timer) {
		elapsed = g_timer_elapsed (priv->timer, NULL);
		if (brasero_task_ctx_get_progress (self, &progress) == BRASERO_BURN_OK) {
			gdouble total_time;

			total_time = (gdouble) elapsed / (gdouble) progress;

			g_mutex_lock (priv->lock);
			priv->total_time = brasero_task_ctx_get_average (&priv->times,
									 total_time);
			g_mutex_unlock (priv->lock);
		}
	}

	if (priv->progress_changed) {
		priv->progress_changed = 0;
		g_signal_emit (self,
			       brasero_task_ctx_signals [PROGRESS_CHANGED_SIGNAL],
			       0);
	}
	else if (priv->written_changed) {
		priv->written_changed = 0;
		g_signal_emit (self,
			       brasero_task_ctx_signals [PROGRESS_CHANGED_SIGNAL],
			       0);
	}
}

BraseroBurnResult
brasero_task_ctx_set_rate (BraseroTaskCtx *self,
			   gint64 rate)
{
	BraseroTaskCtxPrivate *priv;

	g_return_val_if_fail (BRASERO_IS_TASK_CTX (self), BRASERO_BURN_ERR);

	priv = BRASERO_TASK_CTX_PRIVATE (self);
	priv->rate = rate;
	return BRASERO_BURN_OK;
}

/**
 * This is used by jobs that are imaging to tell what's going to be the output 
 * size for a particular track
 */

BraseroBurnResult
brasero_task_ctx_set_output_size_for_current_track (BraseroTaskCtx *self,
						    gint64 sectors,
						    gint64 size)
{
	BraseroTaskCtxPrivate *priv;

	/* NOTE: we don't need block size here as it's pretty easy to have it by
	 * dividing size by sectors or by guessing it with image or audio format
	 * of the output */

	g_return_val_if_fail (BRASERO_IS_TASK_CTX (self), BRASERO_BURN_ERR);

	priv = BRASERO_TASK_CTX_PRIVATE (self);

	/* we only allow plugins to set these values during the init phase of a 
	 * task when it's fakely running. One exception is if size or blocks are
	 * 0 at the start of a task in normal mode */
	if (sectors >= 0)
		priv->blocks += sectors;

	if (size >= 0)
		priv->size += size;

	BRASERO_BURN_LOG ("Task output modified %lli blocks %lli bytes",
			  priv->blocks,
			  priv->size);

	return BRASERO_BURN_OK;
}

BraseroBurnResult
brasero_task_ctx_set_written (BraseroTaskCtx *self,
			      gint64 written)
{
	BraseroTaskCtxPrivate *priv;
	gdouble elapsed = 0.0;

	g_return_val_if_fail (BRASERO_IS_TASK_CTX (self), BRASERO_BURN_ERR);

	priv = BRASERO_TASK_CTX_PRIVATE (self);

	priv->track_bytes = written;
	priv->written_changed = 1;

	if (priv->use_average_rate)
		return BRASERO_BURN_OK;

	if (priv->timer)
		elapsed = g_timer_elapsed (priv->timer, NULL);

	if ((elapsed - priv->last_elapsed) > 0.5) {
		priv->last_written = priv->current_written;
		priv->last_elapsed = priv->current_elapsed;
		priv->current_written = written;
		priv->current_elapsed = elapsed;
	}

	return BRASERO_BURN_OK;
}

BraseroBurnResult
brasero_task_ctx_set_progress (BraseroTaskCtx *self,
			       gdouble progress)
{
	BraseroTaskCtxPrivate *priv;

	g_return_val_if_fail (BRASERO_IS_TASK_CTX (self), BRASERO_BURN_ERR);

	priv = BRASERO_TASK_CTX_PRIVATE (self);
	priv->progress_changed = 1;
	priv->progress = progress;

	return BRASERO_BURN_OK;
}

BraseroBurnResult
brasero_task_ctx_set_current_action (BraseroTaskCtx *self,
				     BraseroBurnAction action,
				     const gchar *string,
				     gboolean force)
{
	BraseroTaskCtxPrivate *priv;

	g_return_val_if_fail (BRASERO_IS_TASK_CTX (self), BRASERO_BURN_ERR);

	priv = BRASERO_TASK_CTX_PRIVATE (self);

	if (!force && priv->current_action == action)
		return BRASERO_BURN_OK;

	g_mutex_lock (priv->lock);

	priv->current_action = action;
	priv->action_changed = 1;

	if (priv->action_string)
		g_free (priv->action_string);

	priv->action_string = string ? g_strdup (string): NULL;

	if (!force) {
		g_slist_free (priv->times);
		priv->times = NULL;
	}

	g_mutex_unlock (priv->lock);

	return BRASERO_BURN_OK;
}

BraseroBurnResult
brasero_task_ctx_set_use_average (BraseroTaskCtx *self,
				  gboolean use_average)
{
	BraseroTaskCtxPrivate *priv;

	g_return_val_if_fail (BRASERO_IS_TASK_CTX (self), BRASERO_BURN_ERR);

	priv = BRASERO_TASK_CTX_PRIVATE (self);
	priv->use_average_rate = use_average;
	return BRASERO_BURN_OK;
}

/**
 * Used to retrieve the values for a given task
 */

BraseroBurnResult
brasero_task_ctx_get_rate (BraseroTaskCtx *self,
			   gint64 *rate)
{
	BraseroTaskCtxPrivate *priv;

	g_return_val_if_fail (BRASERO_IS_TASK_CTX (self), BRASERO_BURN_ERR);
	g_return_val_if_fail (rate != NULL, BRASERO_BURN_ERR);

	priv = BRASERO_TASK_CTX_PRIVATE (self);

	if (priv->current_action != BRASERO_BURN_ACTION_RECORDING
	&&  priv->current_action != BRASERO_BURN_ACTION_DRIVE_COPY) {
		*rate = -1;
		return BRASERO_BURN_OK;
	}

	if (priv->rate) {
		*rate = priv->rate;
		return BRASERO_BURN_OK;
	}

	if (priv->use_average_rate) {
		gdouble elapsed;

		if ((priv->session_bytes + priv->track_bytes) <= 0 || !priv->timer)
			return BRASERO_BURN_NOT_READY;

		elapsed = g_timer_elapsed (priv->timer, NULL);
		*rate = (gdouble) ((priv->session_bytes + priv->track_bytes) - priv->first_written) / elapsed;
	}
	else {
		if (!priv->last_written)
			return BRASERO_BURN_NOT_READY;
			
		*rate = (gdouble) (priv->current_written - priv->last_written) /
			(gdouble) (priv->current_elapsed - priv->last_elapsed);
	}

	return BRASERO_BURN_OK;
}

BraseroBurnResult
brasero_task_ctx_get_remaining_time (BraseroTaskCtx *self,
				     long *remaining)
{
	BraseroTaskCtxPrivate *priv;
	gdouble elapsed;
	gint len;

	g_return_val_if_fail (BRASERO_IS_TASK_CTX (self), BRASERO_BURN_ERR);
	g_return_val_if_fail (remaining != NULL, BRASERO_BURN_ERR);

	priv = BRASERO_TASK_CTX_PRIVATE (self);

	g_mutex_lock (priv->lock);
	len = g_slist_length (priv->times);
	g_mutex_unlock (priv->lock);

	if (len < MAX_VALUE_AVERAGE)
		return BRASERO_BURN_NOT_READY;

	elapsed = g_timer_elapsed (priv->timer, NULL);
	*remaining = (gdouble) priv->total_time - (gdouble) elapsed;

	return BRASERO_BURN_OK;
}

BraseroBurnResult
brasero_task_ctx_get_session_output_size (BraseroTaskCtx *self,
					  gint64 *blocks,
					  gint64 *size)
{
	BraseroTaskCtxPrivate *priv;

	g_return_val_if_fail (BRASERO_IS_TASK_CTX (self), BRASERO_BURN_ERR);
	g_return_val_if_fail (blocks != NULL || size != NULL, BRASERO_BURN_ERR);

	priv = BRASERO_TASK_CTX_PRIVATE (self);

	if (priv->size <= 0 && priv->blocks <= 0)
		return BRASERO_BURN_NOT_READY;

	if (size)
		*size = priv->size;

	if (blocks)
		*blocks = priv->blocks;

	return BRASERO_BURN_OK;
}

BraseroBurnResult
brasero_task_ctx_get_written (BraseroTaskCtx *self,
			      gint64 *written)
{
	BraseroTaskCtxPrivate *priv;

	g_return_val_if_fail (BRASERO_IS_TASK_CTX (self), BRASERO_BURN_ERR);
	g_return_val_if_fail (written != NULL, BRASERO_BURN_ERR);

	priv = BRASERO_TASK_CTX_PRIVATE (self);

	if (priv->track_bytes + priv->session_bytes <= 0)
		return BRASERO_BURN_NOT_READY;

	if (!written)
		return BRASERO_BURN_OK;

	*written = priv->track_bytes + priv->session_bytes;
	return BRASERO_BURN_OK;
}

BraseroBurnResult
brasero_task_ctx_get_current_action_string (BraseroTaskCtx *self,
					    BraseroBurnAction action,
					    gchar **string)
{
	BraseroTaskCtxPrivate *priv;

	g_return_val_if_fail (string != NULL, BRASERO_BURN_ERR);

	priv = BRASERO_TASK_CTX_PRIVATE (self);

	if (action != priv->current_action)
		return BRASERO_BURN_ERR;

	*string = priv->action_string ? g_strdup (priv->action_string):
					g_strdup (brasero_burn_action_to_string (priv->current_action));

	return BRASERO_BURN_OK;
}

BraseroBurnResult
brasero_task_ctx_get_progress (BraseroTaskCtx *self, 
			       gdouble *progress)
{
	BraseroTaskCtxPrivate *priv;
	gdouble track_num = 0;
	gdouble track_nb = 0;
	gint64 total = -1;
	GSList *tracks;

	priv = BRASERO_TASK_CTX_PRIVATE (self);

	tracks = brasero_burn_session_get_tracks (priv->session);
	track_num = g_slist_length (tracks);
	track_nb = g_slist_index (tracks, priv->current_track);	
	if (priv->progress >= 0.0) {
		if (progress)
			*progress = (gdouble) (track_nb + priv->progress) / (gdouble) track_num;

		return BRASERO_BURN_OK;
	}

	brasero_task_ctx_get_session_output_size (self, NULL, &total);
	if ((priv->session_bytes + priv->track_bytes) <= 0 || total <= 0)
		return BRASERO_BURN_NOT_READY;

	if (!progress)
		return BRASERO_BURN_OK;

	*progress = (gdouble) ((gdouble) (priv->track_bytes + priv->session_bytes) / (gdouble)  total);

	return BRASERO_BURN_OK;
}

BraseroBurnResult
brasero_task_ctx_get_current_action (BraseroTaskCtx *self,
				     BraseroBurnAction *action)
{
	BraseroTaskCtxPrivate *priv;

	g_return_val_if_fail (action != NULL, BRASERO_BURN_ERR);

	priv = BRASERO_TASK_CTX_PRIVATE (self);

	g_mutex_lock (priv->lock);
	*action = priv->current_action;
	g_mutex_unlock (priv->lock);

	return BRASERO_BURN_OK;
}

void
brasero_task_ctx_stop_progress (BraseroTaskCtx *self)
{
	BraseroTaskCtxPrivate *priv;

	priv = BRASERO_TASK_CTX_PRIVATE (self);

	/* one last report */
	g_signal_emit (self,
		       brasero_task_ctx_signals [PROGRESS_CHANGED_SIGNAL],
		       0);

	priv->current_action = BRASERO_BURN_ACTION_NONE;
	priv->action_changed = 0;

	if (priv->timer) {
		g_timer_destroy (priv->timer);
		priv->timer = NULL;
	}
	priv->first_written = 0;

	g_mutex_lock (priv->lock);

	if (priv->action_string) {
		g_free (priv->action_string);
		priv->action_string = NULL;
	}

	if (priv->times) {
		g_slist_free (priv->times);
		priv->times = NULL;
	}

	g_mutex_unlock (priv->lock);
}

static void
brasero_task_ctx_init (BraseroTaskCtx *object)
{
	BraseroTaskCtxPrivate *priv;

	priv = BRASERO_TASK_CTX_PRIVATE (object);
	priv->lock = g_mutex_new ();
}

static void
brasero_task_ctx_finalize (GObject *object)
{
	BraseroTaskCtxPrivate *priv;

	priv = BRASERO_TASK_CTX_PRIVATE (object);

	if (priv->lock) {
		g_mutex_free (priv->lock);
		priv->lock = NULL;
	}

	if (priv->timer) {
		g_timer_destroy (priv->timer);
		priv->timer = NULL;
	}

	if (priv->current_track) {
		brasero_track_unref (priv->current_track);
		priv->current_track = NULL;
	}

	if (priv->tracks) {
		g_slist_foreach (priv->tracks, (GFunc) brasero_track_unref, NULL);
		g_slist_free (priv->tracks);
		priv->tracks = NULL;
	}

	if (priv->session) {
		g_object_unref (priv->session);
		priv->session = NULL;
	}

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
brasero_task_ctx_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	BraseroTaskCtx *self;
	BraseroTaskCtxPrivate *priv;

	g_return_if_fail (BRASERO_IS_TASK_CTX (object));

	self = BRASERO_TASK_CTX (object);
	priv = BRASERO_TASK_CTX_PRIVATE (self);

	switch (prop_id)
	{
	case PROP_ACTION:
		priv->action = g_value_get_int (value);
		break;
	case PROP_SESSION:
		priv->session = g_value_get_object (value);
		g_object_ref (priv->session);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
brasero_task_ctx_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	BraseroTaskCtx *self;
	BraseroTaskCtxPrivate *priv;

	g_return_if_fail (BRASERO_IS_TASK_CTX (object));

	self = BRASERO_TASK_CTX (object);
	priv = BRASERO_TASK_CTX_PRIVATE (self);

	switch (prop_id)
	{
	case PROP_ACTION:
		g_value_set_int (value, priv->action);
		break;
	case PROP_SESSION:
		g_value_set_object (value, priv->session);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
brasero_task_ctx_class_init (BraseroTaskCtxClass *klass)
{
	GObjectClass* object_class = G_OBJECT_CLASS (klass);
	parent_class = G_OBJECT_CLASS (g_type_class_peek_parent (klass));

	g_type_class_add_private (klass, sizeof (BraseroTaskCtxPrivate));

	object_class->finalize = brasero_task_ctx_finalize;
	object_class->set_property = brasero_task_ctx_set_property;
	object_class->get_property = brasero_task_ctx_get_property;

	brasero_task_ctx_signals [PROGRESS_CHANGED_SIGNAL] =
	    g_signal_new ("progress_changed",
			  G_TYPE_FROM_CLASS (klass),
			  G_SIGNAL_RUN_LAST,
			  0,
			  NULL, NULL,
			  g_cclosure_marshal_VOID__VOID,
			  G_TYPE_NONE,
			  0);

	brasero_task_ctx_signals [ACTION_CHANGED_SIGNAL] =
	    g_signal_new ("action_changed",
			  G_TYPE_FROM_CLASS (klass),
			  G_SIGNAL_RUN_LAST,
			  0,
			  NULL, NULL,
			  g_cclosure_marshal_VOID__INT,
			  G_TYPE_NONE,
			  1,
			  G_TYPE_INT);

	g_object_class_install_property (object_class,
	                                 PROP_ACTION,
	                                 g_param_spec_int ("action",
							   "The action the task must perform",
							   "The action the task must perform",
							   BRASERO_TASK_ACTION_ERASE,
							   BRASERO_TASK_ACTION_CHECKSUM,
							   BRASERO_TASK_ACTION_NORMAL,
							   G_PARAM_READABLE | G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class,
	                                 PROP_SESSION,
	                                 g_param_spec_object ("session",
	                                                      "The session this object is tied to",
	                                                      "The session this object is tied to",
	                                                      BRASERO_TYPE_BURN_SESSION,
	                                                      G_PARAM_READABLE | G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));
}
