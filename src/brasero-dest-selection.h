/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/*
 * brasero
 * Copyright (C) Philippe Rouquier 2007-2008 <bonfire-app@wanadoo.fr>
 * 
 *  Brasero is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
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

#ifndef _BRASERO_DEST_SELECTION_H_
#define _BRASERO_DEST_SELECTION_H_

#include <glib-object.h>

#include <gtk/gtkwidget.h>

#include "brasero-medium-selection.h"
#include "burn-session.h"

G_BEGIN_DECLS

#define BRASERO_TYPE_DEST_SELECTION             (brasero_dest_selection_get_type ())
#define BRASERO_DEST_SELECTION(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), BRASERO_TYPE_DEST_SELECTION, BraseroDestSelection))
#define BRASERO_DEST_SELECTION_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), BRASERO_TYPE_DEST_SELECTION, BraseroDestSelectionClass))
#define BRASERO_IS_DEST_SELECTION(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), BRASERO_TYPE_DEST_SELECTION))
#define BRASERO_IS_DEST_SELECTION_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), BRASERO_TYPE_DEST_SELECTION))
#define BRASERO_DEST_SELECTION_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), BRASERO_TYPE_DEST_SELECTION, BraseroDestSelectionClass))

typedef struct _BraseroDestSelectionClass BraseroDestSelectionClass;
typedef struct _BraseroDestSelection BraseroDestSelection;

struct _BraseroDestSelectionClass
{
	BraseroMediumSelectionClass parent_class;
};

struct _BraseroDestSelection
{
	BraseroMediumSelection parent_instance;
};

GType brasero_dest_selection_get_type (void) G_GNUC_CONST;

GtkWidget *
brasero_dest_selection_new (BraseroBurnSession *selection);

void
brasero_dest_selection_lock (BraseroDestSelection *selection,
			     gboolean locked);

void
brasero_dest_selection_choose_best (BraseroDestSelection *selection);

G_END_DECLS

#endif /* _BRASERO_DEST_SELECTION_H_ */
