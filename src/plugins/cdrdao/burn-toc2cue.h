/***************************************************************************
 *            burn-toc2cue.h
 *
 *  mar oct  3 18:30:51 2006
 *  Copyright  2006  Philippe Rouquier
 *  bonfire-app@wanadoo.fr
 ***************************************************************************/

/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef BURN_TOC2CUE_H
#define BURN_TOC2CUE_H

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define BRASERO_TYPE_TOC2CUE         (brasero_toc2cue_get_type ())
#define BRASERO_TOC2CUE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), BRASERO_TYPE_TOC2CUE, BraseroToc2Cue))
#define BRASERO_TOC2CUE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), BRASERO_TYPE_TOC2CUE, BraseroToc2CueClass))
#define BRASERO_IS_TOC2CUE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), BRASERO_TYPE_TOC2CUE))
#define BRASERO_IS_TOC2CUE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), BRASERO_TYPE_TOC2CUE))
#define BRASERO_TOC2CUE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), BRASERO_TYPE_TOC2CUE, BraseroToc2CueClass))

G_END_DECLS

#endif /* BURN_TOC2CUE_H */
