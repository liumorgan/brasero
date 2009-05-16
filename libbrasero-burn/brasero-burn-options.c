/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/*
 * Libbrasero-burn
 * Copyright (C) Philippe Rouquier 2005-2009 <bonfire-app@wanadoo.fr>
 *
 * Libbrasero-burn is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * The Libbrasero-burn authors hereby grant permission for non-GPL compatible
 * GStreamer plugins to be used and distributed together with GStreamer
 * and Libbrasero-burn. This permission is above and beyond the permissions granted
 * by the GPL license by which Libbrasero-burn is covered. If you modify this code
 * you may extend this exception to your version of the code, but you are not
 * obligated to do so. If you do not wish to do so, delete this exception
 * statement from your version.
 * 
 * Libbrasero-burn is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Library General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to:
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

#include <gtk/gtk.h>

#include "burn-basics.h"

#include "brasero-medium.h"
#include "brasero-medium-selection-priv.h"

#include "brasero-session.h"
#include "brasero-session-helper.h"
#include "brasero-burn-options.h"
#include "brasero-data-options.h"
#include "brasero-video-options.h"
#include "brasero-src-image.h"
#include "brasero-src-selection.h"
#include "brasero-session-cfg.h"
#include "brasero-dest-selection.h"
#include "brasero-medium-properties.h"
#include "brasero-status-dialog.h"
#include "brasero-track-stream.h"
#include "brasero-track-data-cfg.h"
#include "brasero-track-image-cfg.h"

#include "brasero-notify.h"
#include "brasero-misc.h"

typedef struct _BraseroBurnOptionsPrivate BraseroBurnOptionsPrivate;
struct _BraseroBurnOptionsPrivate
{
	BraseroSessionCfg *session;

	gulong valid_sig;
	gulong input_sig;

	GtkSizeGroup *size_group;

	GtkWidget *source;
	GtkWidget *source_placeholder;
	GtkWidget *message_input;
	GtkWidget *selection;
	GtkWidget *properties;
	GtkWidget *message_output;
	GtkWidget *options;
	GtkWidget *options_placeholder;
	GtkWidget *button;

	guint not_ready_id;
	GtkWidget *status_dialog;

	guint is_valid:1;

	guint has_image:1;
	guint has_audio:1;
	guint has_video:1;
	guint has_data:1;
	guint has_disc:1;
};

#define BRASERO_BURN_OPTIONS_PRIVATE(o)  (G_TYPE_INSTANCE_GET_PRIVATE ((o), BRASERO_TYPE_BURN_OPTIONS, BraseroBurnOptionsPrivate))

enum {
	PROP_0,
	PROP_SESSION
};

G_DEFINE_TYPE (BraseroBurnOptions, brasero_burn_options, GTK_TYPE_DIALOG);

static void
brasero_burn_options_add_source (BraseroBurnOptions *self,
				 const gchar *title,
				 ...)
{
	va_list vlist;
	GtkWidget *child;
	GSList *list = NULL;
	BraseroBurnOptionsPrivate *priv;

	priv = BRASERO_BURN_OPTIONS_PRIVATE (self);

	/* create message queue for input */
	priv->message_input = brasero_notify_new ();
	list = g_slist_prepend (list, priv->message_input);

	va_start (vlist, title);
	while ((child = va_arg (vlist, GtkWidget *))) {
		GtkWidget *hbox;
		GtkWidget *alignment;

		hbox = gtk_hbox_new (FALSE, 12);
		gtk_widget_show (hbox);

		gtk_box_pack_start (GTK_BOX (hbox), child, TRUE, TRUE, 0);

		alignment = gtk_alignment_new (0.0, 0.5, 0., 0.);
		gtk_widget_show (alignment);
		gtk_size_group_add_widget (priv->size_group, alignment);
		gtk_box_pack_start (GTK_BOX (hbox), alignment, FALSE, FALSE, 0);

		list = g_slist_prepend (list, hbox);
	}
	va_end (vlist);

	priv->source = brasero_utils_pack_properties_list (title, list);
	g_slist_free (list);

	gtk_container_add (GTK_CONTAINER (priv->source_placeholder), priv->source);
	gtk_widget_show (priv->source_placeholder);

	brasero_dest_selection_choose_best (BRASERO_DEST_SELECTION (priv->selection));
}

void
brasero_burn_options_add_options (BraseroBurnOptions *self,
				  GtkWidget *options)
{
	BraseroBurnOptionsPrivate *priv;

	priv = BRASERO_BURN_OPTIONS_PRIVATE (self);
	gtk_box_pack_start (GTK_BOX (priv->options), options, FALSE, TRUE, 0);
	gtk_widget_show (priv->options);
}

static GtkWidget *
brasero_burn_options_add_burn_button (BraseroBurnOptions *self,
				      const gchar *text,
				      const gchar *icon)
{
	BraseroBurnOptionsPrivate *priv;

	priv = BRASERO_BURN_OPTIONS_PRIVATE (self);

	if (priv->button) {
		gtk_widget_destroy (priv->button);
		priv->button = NULL;
	}

	priv->button = gtk_dialog_add_button (GTK_DIALOG (self),
					      text,
					      GTK_RESPONSE_OK);
	gtk_button_set_image (GTK_BUTTON (priv->button),
			      gtk_image_new_from_icon_name (icon,
							    GTK_ICON_SIZE_BUTTON));

	return priv->button;
}

static void
brasero_burn_options_lock_selection (BraseroBurnOptions *self)
{
	BraseroBurnOptionsPrivate *priv;

	priv = BRASERO_BURN_OPTIONS_PRIVATE (self);
	brasero_medium_selection_set_active (BRASERO_MEDIUM_SELECTION (priv->selection),
					     brasero_drive_get_medium (brasero_burn_session_get_burner (BRASERO_BURN_SESSION (priv->session))));
	brasero_dest_selection_lock (BRASERO_DEST_SELECTION (priv->selection), TRUE);
}

static void
brasero_burn_options_set_type_shown (BraseroBurnOptions *self,
				     BraseroMediaType type)
{
	BraseroBurnOptionsPrivate *priv;

	priv = BRASERO_BURN_OPTIONS_PRIVATE (self);
	brasero_medium_selection_show_media_type (BRASERO_MEDIUM_SELECTION (priv->selection), type);
}

static void
brasero_burn_options_message_response_cb (BraseroDiscMessage *message,
					  GtkResponseType response,
					  BraseroBurnOptions *self)
{
	if (response == GTK_RESPONSE_OK) {
		BraseroBurnOptionsPrivate *priv;

		priv = BRASERO_BURN_OPTIONS_PRIVATE (self);
		brasero_session_cfg_add_flags (priv->session, BRASERO_BURN_FLAG_OVERBURN);
	}
}

#define BRASERO_BURN_OPTIONS_NO_MEDIUM_WARNING	1000

static void
brasero_burn_options_update_no_medium_warning (BraseroBurnOptions *self)
{
	BraseroBurnOptionsPrivate *priv;

	priv = BRASERO_BURN_OPTIONS_PRIVATE (self);

	if (!priv->is_valid) {
		brasero_notify_message_remove (BRASERO_NOTIFY (priv->message_output),
					       BRASERO_BURN_OPTIONS_NO_MEDIUM_WARNING);
		return;
	}

	if (!brasero_burn_session_is_dest_file (BRASERO_BURN_SESSION (priv->session))) {
		brasero_notify_message_remove (BRASERO_NOTIFY (priv->message_output),
					       BRASERO_BURN_OPTIONS_NO_MEDIUM_WARNING);
		return;
	}

	if (brasero_medium_selection_get_media_num (BRASERO_MEDIUM_SELECTION (priv->selection)) != 1) {
		brasero_notify_message_remove (BRASERO_NOTIFY (priv->message_output),
					       BRASERO_BURN_OPTIONS_NO_MEDIUM_WARNING);
		return;
	}

	/* The user may have forgotten to insert a disc so remind him of that if
	 * there aren't any other possibility in the selection */
	brasero_notify_message_add (BRASERO_NOTIFY (priv->message_output),
				    _("Please insert a recordable CD or DVD if you don't want to write to an image file."),
				    NULL,
				    -1,
				    BRASERO_BURN_OPTIONS_NO_MEDIUM_WARNING);
}

static void
brasero_burn_options_not_ready_dialog_cancel_cb (GtkDialog *dialog,
						 guint response,
						 gpointer data)
{
	gtk_dialog_response (GTK_DIALOG (data), GTK_RESPONSE_CANCEL);
}

static gboolean
brasero_burn_options_not_ready_dialog_show_cb (gpointer data)
{
	BraseroBurnOptionsPrivate *priv;

	priv = BRASERO_BURN_OPTIONS_PRIVATE (data);
	priv->not_ready_id = 0;
	gtk_widget_show (priv->status_dialog);
	return FALSE;
}

static void
brasero_burn_options_update_valid (BraseroBurnOptions *self)
{
	BraseroBurnOptionsPrivate *priv;
	BraseroSessionError valid;

	priv = BRASERO_BURN_OPTIONS_PRIVATE (self);

	valid = brasero_session_cfg_get_error (priv->session);
	priv->is_valid = BRASERO_SESSION_IS_VALID (valid);

	gtk_widget_set_sensitive (priv->button, priv->is_valid);
	gtk_widget_set_sensitive (priv->options, priv->is_valid);
	gtk_widget_set_sensitive (priv->properties, priv->is_valid);

	if (priv->message_input) {
		gtk_widget_hide (priv->message_input);
		brasero_notify_message_remove (BRASERO_NOTIFY (priv->message_input),
					       BRASERO_NOTIFY_CONTEXT_SIZE);
	}

	brasero_notify_message_remove (BRASERO_NOTIFY (priv->message_output),
				       BRASERO_NOTIFY_CONTEXT_SIZE);

	if (valid == BRASERO_SESSION_NOT_READY) {
		if (!priv->not_ready_id && !priv->status_dialog) {
			gtk_widget_set_sensitive (GTK_WIDGET (self), FALSE);
			priv->status_dialog = brasero_status_dialog_new (BRASERO_BURN_SESSION (priv->session),  GTK_WIDGET (self));
			g_signal_connect (priv->status_dialog,
					  "response", 
					  G_CALLBACK (brasero_burn_options_not_ready_dialog_cancel_cb),
					  self);
			priv->not_ready_id = g_timeout_add_seconds (1,
								    brasero_burn_options_not_ready_dialog_show_cb,
								    self);
		}
	}
	else {
		gtk_widget_set_sensitive (GTK_WIDGET (self), TRUE);
		if (priv->status_dialog) {
			gtk_widget_destroy (priv->status_dialog);
			priv->status_dialog = NULL;
		}

		if (priv->not_ready_id) {
			g_source_remove (priv->not_ready_id);
			priv->not_ready_id = 0;
		}
	}

	if (valid == BRASERO_SESSION_INSUFFICIENT_SPACE) {
		brasero_notify_message_add (BRASERO_NOTIFY (priv->message_output),
					    _("Please choose another CD or DVD or insert a new one."),
					    _("The size of the project is too large for the disc even with the overburn option."),
					    -1,
					    BRASERO_NOTIFY_CONTEXT_SIZE);
	}
	else if (valid == BRASERO_SESSION_NO_OUTPUT) {
		brasero_notify_message_add (BRASERO_NOTIFY (priv->message_output),
					    _("Please insert a recordable CD or DVD."),
					    _("There is no recordable disc inserted."),
					    -1,
					    BRASERO_NOTIFY_CONTEXT_SIZE);
	}
	else if (valid == BRASERO_SESSION_NO_CD_TEXT) {
		brasero_notify_message_add (BRASERO_NOTIFY (priv->message_output),
					    _("No track information (artist, title, ...) will be written to the disc."),
					    _("This is not supported by the current active burning backend."),
					    -1,
					    BRASERO_NOTIFY_CONTEXT_SIZE);
	}
	else if (valid == BRASERO_SESSION_EMPTY) {
		BraseroTrackType *type;
		
		type = brasero_track_type_new ();
		brasero_burn_session_get_input_type (BRASERO_BURN_SESSION (priv->session), type);

		if (brasero_track_type_get_has_data (type))
			brasero_notify_message_add (BRASERO_NOTIFY (priv->message_output),
						    _("Please add files."),
						    _("The project is empty"),
						    -1,
						    BRASERO_NOTIFY_CONTEXT_SIZE);
		else if (!BRASERO_STREAM_FORMAT_HAS_VIDEO (brasero_track_type_get_stream_format (type)))
			brasero_notify_message_add (BRASERO_NOTIFY (priv->message_output),
						    _("Please add songs."),
						    _("The project is empty"),
						    -1,
						    BRASERO_NOTIFY_CONTEXT_SIZE);
		else
			brasero_notify_message_add (BRASERO_NOTIFY (priv->message_output),
						     _("Please add videos."),
						    _("The project is empty"),
						    -1,
						    BRASERO_NOTIFY_CONTEXT_SIZE);
		brasero_track_type_free (type);
		return;		      
	}
	else if (valid == BRASERO_SESSION_NO_INPUT_MEDIUM) {
		GtkWidget *message;

		if (priv->message_input) {
			gtk_widget_show (priv->message_input);
			message = brasero_notify_message_add (BRASERO_NOTIFY (priv->message_input),
							      _("Please insert a disc holding data."),
							      _("There is no inserted disc to copy."),
							      -1,
							      BRASERO_NOTIFY_CONTEXT_SIZE);
		}
	}
	else if (valid == BRASERO_SESSION_NO_INPUT_IMAGE) {
		GtkWidget *message;

		if (priv->message_input) {
			gtk_widget_show (priv->message_input);
			message = brasero_notify_message_add (BRASERO_NOTIFY (priv->message_input),
							      _("Please select an image."),
							      _("There is no selected image."),
							      -1,
							      BRASERO_NOTIFY_CONTEXT_SIZE);
		}
	}
	else if (valid == BRASERO_SESSION_UNKNOWN_IMAGE) {
		GtkWidget *message;

		if (priv->message_input) {
			gtk_widget_show (priv->message_input);
			message = brasero_notify_message_add (BRASERO_NOTIFY (priv->message_input),
							      _("Please select another image."),
							      _("It doesn't appear to be a valid image or a valid cue file."),
							      -1,
							      BRASERO_NOTIFY_CONTEXT_SIZE);
		}
	}
	else if (valid == BRASERO_SESSION_DISC_PROTECTED) {
		GtkWidget *message;

		if (priv->message_input) {
			gtk_widget_show (priv->message_input);
			message = brasero_notify_message_add (BRASERO_NOTIFY (priv->message_input),
							      _("Please insert a disc that is not copy protected."),
							      _("Such a disc cannot be copied without the proper plugins."),
							      -1,
							      BRASERO_NOTIFY_CONTEXT_SIZE);
		}
	}
	else if (valid == BRASERO_SESSION_NOT_SUPPORTED) {
		brasero_notify_message_add (BRASERO_NOTIFY (priv->message_output),
					    _("Please replace the disc with a supported CD or DVD."),
					    _("It is not possible to write with the current set of plugins."),
					    -1,
					    BRASERO_NOTIFY_CONTEXT_SIZE);
	}
	else if (valid == BRASERO_SESSION_OVERBURN_NECESSARY) {
		GtkWidget *message;

		message = brasero_notify_message_add (BRASERO_NOTIFY (priv->message_output),
						      _("Would you like to burn beyond the disc reported capacity?"),
						      _("The size of the project is too large for the disc and you must remove files from the project otherwise."
							"\nYou may want to use this option if you're using 90 or 100 min CD-R(W) which cannot be properly recognised and therefore need overburn option."
							"\nNOTE: This option might cause failure."),
						      -1,
						      BRASERO_NOTIFY_CONTEXT_SIZE);
		brasero_notify_button_add (BRASERO_NOTIFY (priv->message_output),
					   BRASERO_DISC_MESSAGE (message),
					   _("_Overburn"),
					   _("Burn beyond the disc reported capacity"),
					   GTK_RESPONSE_OK);

		g_signal_connect (message,
				  "response",
				  G_CALLBACK (brasero_burn_options_message_response_cb),
				  self);
	}
	else if (brasero_burn_session_same_src_dest_drive (BRASERO_BURN_SESSION (priv->session))) {
		/* The medium is valid but it's a special case */
		brasero_notify_message_add (BRASERO_NOTIFY (priv->message_output),
					    _("The drive that holds the source disc will also be the one used to record."),
					    _("A new recordable disc will be required once the one currently loaded has been copied."),
					    -1,
					    BRASERO_NOTIFY_CONTEXT_SIZE);
	}

	brasero_burn_options_update_no_medium_warning (self);
	gtk_window_resize (GTK_WINDOW (self), 10, 10);
}

static void
brasero_burn_options_valid_cb (BraseroSessionCfg *session,
			       BraseroBurnOptions *self)
{
	brasero_burn_options_update_valid (self);
}

static void
brasero_burn_options_init (BraseroBurnOptions *object)
{
	gtk_dialog_set_has_separator (GTK_DIALOG (object), FALSE);
	gtk_window_set_icon_name (GTK_WINDOW (object), "brasero");
}

/**
 * To build anything we need to have the session set first
 */

static void
brasero_burn_options_build_contents (BraseroBurnOptions *object)
{
	BraseroBurnOptionsPrivate *priv;
	GtkWidget *selection;
	GtkWidget *alignment;
	gchar *string;

	priv = BRASERO_BURN_OPTIONS_PRIVATE (object);

	priv->size_group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);

	/* Sets default flags for the session */
	brasero_burn_session_add_flag (BRASERO_BURN_SESSION (priv->session),
				       BRASERO_BURN_FLAG_NOGRACE|
				       BRASERO_BURN_FLAG_CHECK_SIZE);

	/* Create a cancel button */
	gtk_dialog_add_button (GTK_DIALOG (object),
			       GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);

	/* Create a default Burn button */
	priv->button = gtk_dialog_add_button (GTK_DIALOG (object),
					      _("_Burn"),
					      GTK_RESPONSE_OK);
	gtk_button_set_image (GTK_BUTTON (priv->button),
			      gtk_image_new_from_icon_name ("media-optical-burn",
							    GTK_ICON_SIZE_BUTTON));

	/* Create an upper box for sources */
	priv->source_placeholder = gtk_alignment_new (0.0, 0.5, 1.0, 1.0);
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (object)->vbox),
			    priv->source_placeholder,
			    FALSE,
			    TRUE,
			    0);

	/* Medium selection box */
	selection = gtk_hbox_new (FALSE, 12);
	gtk_widget_show (selection);

	alignment = gtk_alignment_new (0.0, 0.5, 1.0, 0.0);
	gtk_widget_show (alignment);
	gtk_box_pack_start (GTK_BOX (selection),
			    alignment,
			    TRUE,
			    TRUE,
			    0);

	priv->selection = brasero_dest_selection_new (BRASERO_BURN_SESSION (priv->session));
	gtk_widget_show (priv->selection);
	gtk_container_add (GTK_CONTAINER (alignment), priv->selection);

	priv->properties = brasero_medium_properties_new (BRASERO_BURN_SESSION (priv->session));
	gtk_size_group_add_widget (priv->size_group, priv->properties);
	gtk_widget_show (priv->properties);
	gtk_box_pack_start (GTK_BOX (selection),
			    priv->properties,
			    FALSE,
			    FALSE,
			    0);

	/* Box to display warning messages */
	priv->message_output = brasero_notify_new ();
	gtk_widget_show (priv->message_output);

	string = g_strdup_printf ("<b>%s</b>", _("Select a disc to write to"));
	selection = brasero_utils_pack_properties (string,
						   priv->message_output,
						   selection,
						   NULL);
	g_free (string);
	gtk_widget_show (selection);

	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (object)->vbox),
			    selection,
			    FALSE,
			    TRUE,
			    0);

	/* Create a lower box for options */
	alignment = gtk_alignment_new (0.0, 0.5, 1.0, 1.0);
	gtk_widget_show (alignment);
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (object)->vbox),
			    alignment,
			    FALSE,
			    TRUE,
			    0);
	priv->options_placeholder = alignment;

	priv->options = gtk_vbox_new (FALSE, 0);
	gtk_container_add (GTK_CONTAINER (alignment), priv->options);

	priv->valid_sig = g_signal_connect (priv->session,
					    "is-valid",
					    G_CALLBACK (brasero_burn_options_valid_cb),
					    object);

	brasero_burn_options_update_valid (object);
}

static void
brasero_burn_options_finalize (GObject *object)
{
	BraseroBurnOptionsPrivate *priv;

	priv = BRASERO_BURN_OPTIONS_PRIVATE (object);

	if (priv->not_ready_id) {
		g_source_remove (priv->not_ready_id);
		priv->not_ready_id = 0;
	}

	if (priv->status_dialog) {
		gtk_widget_destroy (priv->status_dialog);
		priv->status_dialog = NULL;
	}

	if (priv->input_sig) {
		g_signal_handler_disconnect (priv->session,
					     priv->input_sig);
		priv->input_sig = 0;
	}

	if (priv->valid_sig) {
		g_signal_handler_disconnect (priv->session,
					     priv->valid_sig);
		priv->valid_sig = 0;
	}

	if (priv->session) {
		g_object_unref (priv->session);
		priv->session = NULL;
	}

	if (priv->size_group) {
		g_object_unref (priv->size_group);
		priv->size_group = NULL;
	}

	G_OBJECT_CLASS (brasero_burn_options_parent_class)->finalize (object);
}

static void
brasero_burn_options_reset (BraseroBurnOptions *self)
{
	BraseroBurnOptionsPrivate *priv;

	priv = BRASERO_BURN_OPTIONS_PRIVATE (self);

	priv->has_image = FALSE;
	priv->has_audio = FALSE;
	priv->has_video = FALSE;
	priv->has_data = FALSE;
	priv->has_disc = FALSE;

	/* reset all the dialog */
	if (priv->message_input) {
		gtk_widget_destroy (priv->message_input);
		priv->message_input = NULL;
	}

	if (priv->options) {
		gtk_widget_destroy (priv->options);
		priv->options = NULL;
	}

	priv->options = gtk_vbox_new (FALSE, 0);
	gtk_container_add (GTK_CONTAINER (priv->options_placeholder), priv->options);

	if (priv->source) {
		gtk_widget_destroy (priv->source);
		priv->source = NULL;
	}

	gtk_widget_hide (priv->source_placeholder);
}

static void
brasero_burn_options_setup_audio (BraseroBurnOptions *self)
{
	BraseroBurnOptionsPrivate *priv;

	priv = BRASERO_BURN_OPTIONS_PRIVATE (self);

	brasero_burn_options_reset (self);

	priv->has_audio = TRUE;
	gtk_window_set_title (GTK_WINDOW (self), _("Disc Burning Setup"));
	brasero_burn_options_add_burn_button (self,
					      _("_Burn"),
					      "media-optical-burn");
	brasero_burn_options_set_type_shown (BRASERO_BURN_OPTIONS (self),
					     BRASERO_MEDIA_TYPE_WRITABLE);
}

static void
brasero_burn_options_setup_video (BraseroBurnOptions *self)
{
	GtkWidget *options;
	BraseroBurnOptionsPrivate *priv;

	priv = BRASERO_BURN_OPTIONS_PRIVATE (self);

	brasero_burn_options_reset (self);

	priv->has_video = TRUE;
	gtk_window_set_title (GTK_WINDOW (self), _("Disc Burning Setup"));
	brasero_burn_options_add_burn_button (self,
					      _("_Burn"),
					      "media-optical-burn");
	brasero_burn_options_set_type_shown (BRASERO_BURN_OPTIONS (self),
					     BRASERO_MEDIA_TYPE_WRITABLE|
					     BRASERO_MEDIA_TYPE_FILE);

	/* create the options box */
	options = brasero_video_options_new (BRASERO_BURN_SESSION (priv->session));
	gtk_widget_show (options);
	brasero_burn_options_add_options (self, options);
}

static BraseroBurnResult
brasero_status_dialog_uri_has_image (BraseroTrackDataCfg *track,
				     const gchar *uri,
				     BraseroBurnOptions *self)
{
	gint answer;
	gchar *name;
	gchar *string;
	GtkWidget *button;
	GtkWidget *dialog;
	gboolean was_visible = FALSE;
	gboolean was_not_ready = FALSE;
	BraseroTrackImageCfg *track_img;
	BraseroBurnOptionsPrivate *priv;

	priv = BRASERO_BURN_OPTIONS_PRIVATE (self);

	name = brasero_utils_get_uri_name (uri);
	string = g_strdup_printf (_("Do you want to burn \"%s\" to a disc or add it in to the data project?"), name);
	dialog = gtk_message_dialog_new (GTK_WINDOW (self),
					 GTK_DIALOG_MODAL |
					 GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_MESSAGE_QUESTION,
					 GTK_BUTTONS_NONE,
					 "%s",
					 string);
	g_free (string);
	g_free (name);

	gtk_window_set_title (GTK_WINDOW (dialog), "");
	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
						  _("This file is the image of a disc and can therefore be burnt to disc without having to add it to a data project first."));

	gtk_dialog_add_button (GTK_DIALOG (dialog), _("_Add to Project"), GTK_RESPONSE_NO);

	button = brasero_utils_make_button (_("_Burn..."),
					    NULL,
					    "media-optical-burn",
					    GTK_ICON_SIZE_BUTTON);
	gtk_widget_show (button);
	gtk_dialog_add_action_widget (GTK_DIALOG (dialog),
				      button,
				      GTK_RESPONSE_YES);

	if (!priv->not_ready_id && priv->status_dialog) {
		was_visible = TRUE;
		gtk_widget_hide (GTK_WIDGET (priv->status_dialog));
	}
	else if (priv->not_ready_id) {
		g_source_remove (priv->not_ready_id);
		priv->not_ready_id = 0;
		was_not_ready = TRUE;
	}

	gtk_widget_show_all (dialog);
	answer = gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);

	if (answer != GTK_RESPONSE_YES) {
		if (was_not_ready)
			priv->not_ready_id = g_timeout_add_seconds (1,
								    brasero_burn_options_not_ready_dialog_show_cb,
								    self);
		if (was_visible)
			gtk_widget_show (GTK_WIDGET (priv->status_dialog));

		return BRASERO_BURN_OK;
	}

	/* Setup a new track and add it to session */
	track_img = brasero_track_image_cfg_new ();
	brasero_track_image_cfg_set_source (track_img, uri);
	brasero_burn_session_add_track (BRASERO_BURN_SESSION (priv->session),
					BRASERO_TRACK (track_img));

	return BRASERO_BURN_CANCEL;
}

static void
brasero_burn_options_setup_data (BraseroBurnOptions *self)
{
	GSList *tracks;
	GtkWidget *options;
	BraseroBurnOptionsPrivate *priv;

	priv = BRASERO_BURN_OPTIONS_PRIVATE (self);

	brasero_burn_options_reset (self);

	/* NOTE: we don't need to keep a record of the signal as the track will
	 * be destroyed if the user agrees to burn the image directly */
	tracks = brasero_burn_session_get_tracks (BRASERO_BURN_SESSION (priv->session));
	if (BRASERO_IS_TRACK_DATA_CFG (tracks->data))
		g_signal_connect (tracks->data,
				  "image-uri",
				  G_CALLBACK (brasero_status_dialog_uri_has_image),
				  self);

	priv->has_data = TRUE;
	gtk_window_set_title (GTK_WINDOW (self), _("Disc Burning Setup"));
	brasero_burn_options_add_burn_button (self,
					      _("_Burn"),
					      "media-optical-burn");
	brasero_burn_options_set_type_shown (BRASERO_BURN_OPTIONS (self),
					     BRASERO_MEDIA_TYPE_WRITABLE|
					     BRASERO_MEDIA_TYPE_FILE);

	/* create the options box */
	options = brasero_data_options_new (BRASERO_BURN_SESSION (priv->session));
	gtk_widget_show (options);
	brasero_burn_options_add_options (self, options);
}

static void
brasero_burn_options_setup_image (BraseroBurnOptions *self)
{
	gchar *string;
	GtkWidget *file;
	BraseroBurnOptionsPrivate *priv;

	priv = BRASERO_BURN_OPTIONS_PRIVATE (self);

	brasero_burn_options_reset (self);

	priv->has_image = TRUE;
	gtk_window_set_title (GTK_WINDOW (self), _("Image Burning Setup"));
	brasero_burn_options_add_burn_button (self,
					      _("_Burn"),
					      "media-optical-burn");
	brasero_burn_options_set_type_shown (self, BRASERO_MEDIA_TYPE_WRITABLE);

	/* Image properties */
	file = brasero_src_image_new (BRASERO_BURN_SESSION (priv->session));
	gtk_widget_show (file);

	/* pack everything */
	string = g_strdup_printf ("<b>%s</b>", _("Select an image to write"));
	brasero_burn_options_add_source (self, 
					 string,
					 file,
					 NULL);
	g_free (string);
}

static void
brasero_burn_options_setup_disc (BraseroBurnOptions *self)
{
	gchar *title_str;
	GtkWidget *source;
	BraseroBurnOptionsPrivate *priv;

	priv = BRASERO_BURN_OPTIONS_PRIVATE (self);

	brasero_burn_options_reset (self);

	priv->has_disc = TRUE;
	gtk_window_set_title (GTK_WINDOW (self), _("CD/DVD Copy Options"));
	brasero_burn_options_add_burn_button (self,
					      _("_Copy"),
					      "media-optical-copy");

	/* take care of source media */
	source = brasero_src_selection_new (BRASERO_BURN_SESSION (priv->session));
	gtk_widget_show (source);

	title_str = g_strdup_printf ("<b>%s</b>", _("Select disc to copy"));
	brasero_burn_options_add_source (self,
					 title_str,
					 source,
					 NULL);
	g_free (title_str);

	/* only show media with something to be read on them */
	brasero_medium_selection_show_media_type (BRASERO_MEDIUM_SELECTION (source),
						  BRASERO_MEDIA_TYPE_AUDIO|
						  BRASERO_MEDIA_TYPE_DATA);

	/* This is a special case. When we're copying, someone may want to read
	 * and burn to the same drive so provided that the drive is a burner
	 * then show its contents. */
	brasero_burn_options_set_type_shown (self,
					     BRASERO_MEDIA_TYPE_ANY_IN_BURNER|
					     BRASERO_MEDIA_TYPE_FILE);
}

static void
brasero_burn_options_setup (BraseroBurnOptions *self)
{
	BraseroBurnOptionsPrivate *priv;
	BraseroTrackType *type;

	priv = BRASERO_BURN_OPTIONS_PRIVATE (self);

	/* add the new widgets */
	type = brasero_track_type_new ();
	brasero_burn_session_get_input_type (BRASERO_BURN_SESSION (priv->session), type);
	if (brasero_track_type_get_has_medium (type)) {
		if (!priv->has_disc)
			brasero_burn_options_setup_disc (self);
	}
	else if (brasero_track_type_get_has_image (type)) {
		if (!priv->has_image)
			brasero_burn_options_setup_image (self);
	}
	else if (brasero_track_type_get_has_data (type)) {
		if (!priv->has_data)
			brasero_burn_options_setup_data (self);
	}
	else if (brasero_track_type_get_has_stream (type)) {
		if (brasero_track_type_get_stream_format (type) & (BRASERO_VIDEO_FORMAT_UNDEFINED|BRASERO_VIDEO_FORMAT_VCD|BRASERO_VIDEO_FORMAT_VIDEO_DVD)) {
			if (!priv->has_video)
				brasero_burn_options_setup_video (self);
		}
		else if (!priv->has_audio)
			brasero_burn_options_setup_audio (self);
	}
	brasero_track_type_free (type);

	/* see if we should lock the drive only with MERGE */
	if (brasero_burn_session_get_flags (BRASERO_BURN_SESSION (priv->session)) & BRASERO_BURN_FLAG_MERGE)
		brasero_burn_options_lock_selection (self);
}

static void
brasero_burn_options_input_changed (BraseroBurnSession *session,
				    BraseroBurnOptions *dialog)
{
	brasero_burn_options_setup (dialog);
}

static void
brasero_burn_options_set_property (GObject *object,
				   guint prop_id,
				   const GValue *value,
				   GParamSpec *pspec)
{
	BraseroBurnOptionsPrivate *priv;

	g_return_if_fail (BRASERO_IS_BURN_OPTIONS (object));

	priv = BRASERO_BURN_OPTIONS_PRIVATE (object);

	switch (prop_id)
	{
	case PROP_SESSION: /* Readable and only writable at creation time */
		priv->session = BRASERO_SESSION_CFG (g_value_get_object (value));
		g_object_ref (priv->session);
		g_object_notify (object, "session");

		priv->input_sig = g_signal_connect (priv->session,
						    "input-changed",
						    G_CALLBACK (brasero_burn_options_input_changed),
						    object);

		brasero_burn_options_build_contents (BRASERO_BURN_OPTIONS (object));
		brasero_burn_options_setup (BRASERO_BURN_OPTIONS (object));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
brasero_burn_options_get_property (GObject *object,
				   guint prop_id,
				   GValue *value,
				   GParamSpec *pspec)
{
	BraseroBurnOptionsPrivate *priv;

	g_return_if_fail (BRASERO_IS_BURN_OPTIONS (object));

	priv = BRASERO_BURN_OPTIONS_PRIVATE (object);

	switch (prop_id)
	{
	case PROP_SESSION:
		g_value_set_object (value, priv->session);
		g_object_ref (priv->session);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
brasero_burn_options_class_init (BraseroBurnOptionsClass *klass)
{
	GObjectClass* object_class = G_OBJECT_CLASS (klass);

	g_type_class_add_private (klass, sizeof (BraseroBurnOptionsPrivate));

	object_class->finalize = brasero_burn_options_finalize;
	object_class->set_property = brasero_burn_options_set_property;
	object_class->get_property = brasero_burn_options_get_property;

	g_object_class_install_property (object_class,
					 PROP_SESSION,
					 g_param_spec_object ("session",
							      "The session",
							      "The session to work with",
							      BRASERO_TYPE_BURN_SESSION,
							      G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));
}

GtkWidget *
brasero_burn_options_new (BraseroSessionCfg *session)
{
	return g_object_new (BRASERO_TYPE_BURN_OPTIONS, "session", session, NULL);
}