/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  nsc-converter.c
 * 
 *  Copyright (C) 2008-2012 Brian Pepple
 *  Copyright (C) 2003 Ross Burton
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  Author: Brian Pepple <bpepple@fedoraproject.org>
 *          Ross Burton <ross@burtonini.com>
 * 
 */

#include <config.h>

#include <sys/time.h>
#include <string.h>

#include <glib/gi18n.h>
#include <gio/gio.h>
#include <gtk/gtk.h>
#include <gst/gst.h>
#include <libcaja-extension/caja-file-info.h>

#include "nsc-converter.h"
#include "nsc-gstreamer.h"
#include "nsc-xml.h"
#include "rb-gst-media-types.h"

typedef struct _NscConverterPrivate NscConverterPrivate;

typedef struct {
	int            seconds;
	struct timeval time;
	int            ripped;
	int            taken;
} Progress;

struct _NscConverterPrivate {
	/* GStreamer Object */
	NscGStreamer	*gst;

	/* The current audio profile */
	GstEncodingProfile *profile;

	GtkWidget	*dialog;
	GtkWidget	*path_chooser;
	GtkWidget       *profile_chooser;
	GtkWidget       *progress_dlg;
	GtkWidget       *progressbar;
	GtkWidget       *speedbar;

	/* Status icon */
	GtkStatusIcon   *status_icon;
	
	/* Files to be convertered */
	GList		*files;
	gint             files_converted;
	gint		 total_files;

	/* Use the source directory as the output directory? */
	gboolean         src_dir;

	/* Directory to save new file */
	gchar           *save_path;

	/* Snapshots of the progress used to calculate the speed and the ETA */
	Progress         before;

        /* The duration of the file being converter. */
	gint             current_duration;

	/* The total duration of the file being converter. */
	gint             total_duration;
};

/* Default profile name */
#define DEFAULT_MEDIA_TYPE "audio/x-vorbis"

/*
 * gsettings key for whether the user wants to use
 * the source directory for the output directory.
 */
#define SOURCE_DIRECTORY "org.mate.caja-sound-converter"

#define NSC_CONVERTER_GET_PRIVATE(o)           \
	((NscConverterPrivate *)((NSC_CONVERTER(o))->priv))

G_DEFINE_TYPE (NscConverter, nsc_converter, G_TYPE_OBJECT)

enum {
	PROP_FILES = 1,
};

static void
nsc_converter_finalize (GObject *object)
{
	NscConverter 	    *self = (NscConverter *) object;
	NscConverterPrivate *priv = NSC_CONVERTER_GET_PRIVATE (self);

	if (priv != NULL) {
		if (priv->save_path)
			g_free (priv->save_path);

		if (priv->gst)
			g_object_unref (priv->gst);

		if (priv->profile)
			g_object_unref (priv->profile);

		if (priv->files)
			g_list_free (priv->files);

		g_free (priv);

		(NSC_CONVERTER (self))->priv = NULL;
	}

	G_OBJECT_CLASS (nsc_converter_parent_class)->finalize(object);
}

static void
nsc_converter_set_property (GObject      *object,
			    guint         property_id,
			    const GValue *value,
			    GParamSpec   *pspec)
{
	NscConverter        *self = NSC_CONVERTER (object);
	NscConverterPrivate *priv = NSC_CONVERTER_GET_PRIVATE (self);

	switch (property_id) {
	case PROP_FILES:
		priv->files = g_value_get_pointer (value);
		priv->total_files = g_list_length (priv->files);
		break;
	default:
		/* We don't have any other property... */
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
nsc_converter_get_property (GObject    *object,
			    guint       property_id,
			    GValue     *value,
			    GParamSpec *pspec)
{
	NscConverter	    *self = NSC_CONVERTER (object);
	NscConverterPrivate *priv = NSC_CONVERTER_GET_PRIVATE (self);

	switch (property_id) {
	case PROP_FILES:
		g_value_set_pointer (value, priv->files);
		break;
	default:
		/* We don't have any other property... */
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
nsc_converter_class_init (NscConverterClass *klass)
{
	g_type_class_add_private (klass, sizeof (NscConverterPrivate));

	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GParamSpec   *files_param_spec;

	object_class->finalize = nsc_converter_finalize;
	object_class->set_property = nsc_converter_set_property;
	object_class->get_property = nsc_converter_get_property;

	files_param_spec =
		g_param_spec_pointer ("files",
				      "Files",
				      "Set selected files",
				      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);

	g_object_class_install_property (object_class,
					 PROP_FILES,
					 files_param_spec);
}

/**
 * Cancel converting the files.
 */
static void
progress_cancel_cb (GtkWidget *widget, gpointer user_data)
{
	NscConverter        *conv;
	NscConverterPrivate *priv;

	conv =  NSC_CONVERTER (user_data);
	priv =  NSC_CONVERTER_GET_PRIVATE (conv);

	nsc_gstreamer_cancel_convert (priv->gst);

	gtk_widget_destroy (priv->progress_dlg);
	if (priv->status_icon)
		g_object_unref (priv->status_icon);

	g_object_unref (priv->gst);
	priv->gst = NULL;
}

/**
 * Create the progress dialog
 */
static void
create_progress_dialog (NscConverter *converter)
{
	NscConverterPrivate *priv;
	GtkWidget           *button;

	priv = NSC_CONVERTER_GET_PRIVATE (converter);

	/* Create the gtkbuilder, and grab the widgets */
	nsc_xml_get_file ("progress.ui",
			  "progress_dialog", &priv->progress_dlg,
			  "file_progressbar", &priv->progressbar,
			  "speed_progressbar", &priv->speedbar,
			  "cancel_button", &button,
			  NULL);

	/* Connect the signal for the cancel button */
	g_signal_connect (G_OBJECT (button), "clicked",
			  (GCallback) progress_cancel_cb,
			  converter);

	gtk_widget_show_all (priv->progress_dlg);
}

/**
 * Create the new GFile.  This will need to be unreferenced.
 */
static GFile *
create_new_file (NscConverter *converter, GFile *file)
{
	NscConverterPrivate *priv;
	GFile               *new_file;
	gchar               *media_type;
	gchar               *old_basename, *new_basename;
	gchar               *extension, *new_uri;
	const gchar         *new_extension;

	g_return_val_if_fail (G_IS_FILE (file), NULL);

	priv = NSC_CONVERTER_GET_PRIVATE (converter);

	/* Let's the get the basename from the original file */
	old_basename = g_file_get_basename (file);

	/* Now let's remove the extension from the basename. */
	extension = g_strdup (strrchr (old_basename, '.'));
	if (extension != NULL)
		old_basename[strlen (old_basename) - strlen (extension)] = '\0';
	g_free (extension);

	/* Get the new extension from the audio profie */
	media_type = rb_gst_encoding_profile_get_media_type (priv->profile);
	new_extension = rb_gst_media_type_to_extension (media_type);
	g_free (media_type);

	/* Create the new basename */
	new_basename = g_strdup_printf ("%s.%s", old_basename, new_extension);
	g_free (old_basename);

	/* Now let's create the new files uri */
	new_uri = g_strconcat (priv->save_path, G_DIR_SEPARATOR_S,
			       new_basename, NULL);
	g_free (new_basename);

	/* And now finally let's create the new GFile */
	new_file = g_file_new_for_uri (new_uri);
	g_free (new_uri);

	return new_file;
}

/**
 * Function to get orginal & new files, and pass
 * them to the gstreamer object.
 */
static void
convert_file (NscConverter *convert)
{
	NscConverterPrivate *priv;
	CajaFileInfo    *file_info;
	GFile               *old_file, *new_file;
	GError              *err = NULL;

	priv = NSC_CONVERTER_GET_PRIVATE (convert);

	g_return_if_fail (priv->files != NULL);

	/* Get the files */
	file_info = CAJA_FILE_INFO (priv->files->data);
	old_file = caja_file_info_get_location (file_info);
	new_file = create_new_file (convert, old_file);

	/* Let's finally get to the fun stuff */
	nsc_gstreamer_convert_file (priv->gst, old_file, new_file,
				    &err);

	/* Free the files since we do not need them anymore */
	g_object_unref (old_file);
	g_object_unref (new_file);
}

/**
 * Update progressbar text
 */
static void
update_progressbar_text (NscConverter *convert)
{
	NscConverterPrivate *priv;
	gchar               *text;

	g_return_if_fail (NSC_IS_CONVERTER (convert));

	priv = NSC_CONVERTER_GET_PRIVATE (convert);

	text = g_strdup_printf (dgettext (GETTEXT_PACKAGE, "Converting: %d of %d"),
				priv->files_converted + 1, priv->total_files);
	gtk_progress_bar_set_text (GTK_PROGRESS_BAR (priv->progressbar),
				   text);
	if (priv->status_icon) {
		gtk_status_icon_set_tooltip_text (priv->status_icon,
						  text);
	}
	g_free (text);
}

/** 
 * Callback to report errors.  The error passed in does not
 * need to be freed.
 */
static void
on_error_cb (NscGStreamer *gstream, GError *error, gpointer data)
{
	NscConverter	    *converter;
	NscConverterPrivate *priv;
	GtkWidget           *dialog;
	gchar               *text;

	converter = NSC_CONVERTER (data);
	priv = NSC_CONVERTER_GET_PRIVATE (converter);

	text = g_strdup_printf (dgettext (GETTEXT_PACKAGE, "Caja Sound Converter could "
					  "not convert this file.\nReason: %s"),
				error->message);

	dialog = gtk_message_dialog_new (GTK_WINDOW (priv->dialog), 0,
					 GTK_MESSAGE_ERROR,
					 GTK_BUTTONS_CLOSE,
					 text);
	g_free (text);

	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);
}

/**
 * Callback to report completion.
 */
static void
on_completion_cb (NscGStreamer *gstream, gpointer data)
{
	NscConverter	    *converter;
	NscConverterPrivate *priv;
	gdouble              fraction;

	converter = NSC_CONVERTER (data);
	priv = NSC_CONVERTER_GET_PRIVATE (converter);
	
	/* Increment converted total & point to next file */
	priv->files_converted++;
	priv->files = priv->files->next;

	/* Reset the progress variables */
	priv->current_duration =  0;
	priv->total_duration   =  0;
	priv->before.seconds   = -1;

	/* Clear the speed label */
	gtk_progress_bar_set_text (GTK_PROGRESS_BAR (priv->speedbar),
				   (dgettext (GETTEXT_PACKAGE, "Speed: Unknown")));

	/* Update the progress dialog */
	fraction = (double) priv->files_converted / priv->total_files;
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (priv->progressbar),
				       fraction);
	update_progressbar_text (converter);

	/* If there are more files let's go ahead and convert them */
	if (priv->files != NULL) {
		convert_file (converter);
	} else {
		/* No more files to convert time to do some cleanup */
		gtk_widget_destroy (priv->progress_dlg);
		if (priv->status_icon)
			g_object_unref (priv->status_icon);
		g_object_unref (priv->gst);
		priv->gst = NULL;
	}
}

/**
 * Callback to set file total duration.
 */
static void
on_duration_cb (NscGStreamer *gstream,
		const int     seconds,
		gpointer      data)
{
	NscConverter        *conv;
	NscConverterPrivate *priv;

	conv = NSC_CONVERTER (data);
	priv = NSC_CONVERTER_GET_PRIVATE (conv);

	priv->total_duration = seconds;
}

/**
 * Update the ETA and Speed labels
 */
static void
update_speed_progress (NscConverter *conv,
		       float         speed,
		       int           eta)
{
	NscConverterPrivate *priv;
	gchar               *eta_str;

	priv = NSC_CONVERTER_GET_PRIVATE (conv);

	if (eta >= 0) {
		eta_str =
			g_strdup_printf (dgettext (GETTEXT_PACKAGE, 
						   "Estimated time left: %d:%02d (at %0.1f\303\227)"),
					 eta / 60,
					 eta % 60,
					 speed);
	} else {
		eta_str = g_strdup (dgettext (GETTEXT_PACKAGE, "Estimated time left: unknown"));
	}

	gtk_progress_bar_set_text (GTK_PROGRESS_BAR (priv->speedbar),
				   eta_str);
	g_free (eta_str);
}

/**
 * Callback to report on file conversion progress.
 */
static void
on_progress_cb (NscGStreamer *gstream,
		const int     seconds,
		gpointer      data)
{
	NscConverter        *conv;
	NscConverterPrivate *priv;

	conv = NSC_CONVERTER (data);
	priv = NSC_CONVERTER_GET_PRIVATE (conv);

	if (priv->total_duration != 0) {
		float percent;

		percent =
			CLAMP ((float) (priv->current_duration + seconds)
			       / (float) priv->total_duration, 0, 1);
		gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (priv->speedbar), percent);

		if (priv->before.seconds == -1) {
			priv->before.seconds = priv->current_duration + seconds;
			gettimeofday (&priv->before.time, NULL);
		} else {
			struct timeval time;
			gint           taken;
			float          speed;

			gettimeofday (&time, NULL);
			taken = time.tv_sec + (time.tv_usec / 1000000.0)
				- (priv->before.time.tv_sec + (priv->before.time.tv_usec / 1000000.0));

			if (taken >= 2) {
				priv->before.taken += taken;
				priv->before.ripped += priv->current_duration + seconds - priv->before.seconds;
				speed = (float) priv->before.ripped / (float) priv->before.taken;
				update_speed_progress (conv, speed,
						       (int) ((priv->total_duration - priv->current_duration - seconds)
							      / speed));
				priv->before.seconds = priv->current_duration + seconds;
				gettimeofday (&priv->before.time, NULL);
			}
		}
	}
}

static void
converter_status_icon_activate_cb (GtkStatusIcon *status_icon,
				   NscConverter  *converter)
{
	NscConverterPrivate *priv;
	gboolean             visible;

	priv = NSC_CONVERTER_GET_PRIVATE (converter);

	g_object_get (priv->progress_dlg,
		      "visible", &visible,
		      NULL);

	if (visible && gtk_status_icon_is_embedded (status_icon)) {
		gtk_widget_hide (priv->progress_dlg);
	} else {
		gtk_widget_show_all (priv->progress_dlg);
	}
}

static void
create_gst (NscConverter *conv)
{
	NscConverterPrivate *priv;

	priv = NSC_CONVERTER_GET_PRIVATE (conv);

	priv->gst = nsc_gstreamer_new (priv->profile);
	
	/* Connect to the gstreamer object signals */
	g_signal_connect (G_OBJECT (priv->gst), "completion",
			  (GCallback) on_completion_cb,
			  conv);
	g_signal_connect (G_OBJECT (priv->gst), "error",
			  (GCallback) on_error_cb,
			  conv);
	g_signal_connect (G_OBJECT (priv->gst), "progress",
			  (GCallback) on_progress_cb,
			  conv);
	g_signal_connect (G_OBJECT (priv->gst), "duration",
			  (GCallback) on_duration_cb,
			  conv);
}

static void
create_status_icon (NscConverter *conv)
{
	NscConverterPrivate *priv;

	priv = NSC_CONVERTER_GET_PRIVATE (conv);

	priv->status_icon = gtk_status_icon_new_from_icon_name ("gtk-convert");
	g_signal_connect (priv->status_icon,
			  "activate",
			  G_CALLBACK (converter_status_icon_activate_cb),
			  conv);
	gtk_status_icon_set_visible (priv->status_icon, TRUE);
}
	
/**
 * The OK or Cancel button was pressed on the main dialog.
 */
static void
converter_response_cb (GtkWidget *dialog,
		       gint       response_id,
		       gpointer   user_data)
{
	if (response_id == GTK_RESPONSE_OK) {
		NscConverter	    *converter;
		NscConverterPrivate *priv;

		GtkTreeIter iter;
		GtkTreeModel *model;

		converter = NSC_CONVERTER (user_data);
		priv = NSC_CONVERTER_GET_PRIVATE (converter);

		/* Grab the save path */
		priv->save_path =
			g_strdup (gtk_file_chooser_get_uri
				  (GTK_FILE_CHOOSER (priv->path_chooser)));
	       
		/* Grab the encoding profile choosen */
		model = gtk_combo_box_get_model (GTK_COMBO_BOX (priv->profile_chooser));
		if (gtk_combo_box_get_active_iter
		    (GTK_COMBO_BOX (priv->profile_chooser), &iter)) {
			gchar *media_type;

			gtk_tree_model_get (GTK_TREE_MODEL (model), &iter,
					    0, &media_type, -1);
			priv->profile = rb_gst_get_encoding_profile (media_type);
			g_free (media_type);
		}
	      
		/* This probably isn't necessary, but let's leave it for now */
		if (!(nsc_gstreamer_supports_profile (priv->profile))) {
			/*
			 * TODO: Add a message dialog to tell the user
			 *       the selected profile is not supported.
			 */
			return;
		}

		/* Create the gstreamer converter object */
		create_gst (converter);

		/* Create the progress window & status icon */
		create_progress_dialog (converter);
		create_status_icon (converter);

		/* Let's put some text in the progressbar */
		update_progressbar_text (converter);
		gtk_progress_bar_set_text (GTK_PROGRESS_BAR (priv->speedbar), 
					   (dgettext (GETTEXT_PACKAGE, "Speed: Unknown")));

		/* Alright we're finally ready to start converting */
		convert_file (converter);
	}
	gtk_widget_destroy (dialog);
}

static GtkWidget
*nsc_audio_profile_chooser_new (void)
{
	GstEncodingTarget *target;
	const GList       *p;
	GtkWidget         *combo_box;
	GtkCellRenderer   *renderer;
	GtkTreeModel      *model;

	model = GTK_TREE_MODEL (gtk_tree_store_new
				(3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_POINTER));
	target = rb_gst_get_default_encoding_target ();

	for (p = gst_encoding_target_get_profiles (target); p != NULL; p = p->next) {
		GstEncodingProfile *profile;
		gchar *media_type;

		profile = GST_ENCODING_PROFILE (p->data);
		media_type = rb_gst_encoding_profile_get_media_type (profile);
		if (media_type == NULL) {
			continue;
		}
		gtk_tree_store_insert_with_values (GTK_TREE_STORE (model),
						   NULL, NULL, -1,
						   0, media_type,
						   1, gst_encoding_profile_get_description (profile),
						   2, profile, -1);
		g_free (media_type);
	}

	combo_box = gtk_combo_box_new_with_model (model);
	renderer = gtk_cell_renderer_text_new ();
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (combo_box), renderer, TRUE);
	gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (combo_box),
					renderer, "text", 1, NULL);

	return GTK_WIDGET (combo_box);
}

static void
nsc_audio_profile_chooser_set_active (GtkWidget *chooser, const char *profile)
{
	GtkTreeIter   iter;
	GtkTreeModel *model;
	gboolean      done;

	done = FALSE;
	model = gtk_combo_box_get_model (GTK_COMBO_BOX (chooser));
	if (gtk_tree_model_get_iter_first (model, &iter)) {
		do {
			gchar *media_type;

			gtk_tree_model_get (model, &iter, 0, &media_type, -1);
			if (g_strcmp0 (media_type, profile) == 0) {
				gtk_combo_box_set_active_iter (GTK_COMBO_BOX (chooser), &iter);
				done = TRUE;
			}
			g_free (media_type);
		} while (done == FALSE && gtk_tree_model_iter_next (model, &iter));
	}

	if (done == FALSE) {
		gtk_combo_box_set_active_iter (GTK_COMBO_BOX (chooser), NULL);
	}
}

static void
create_main_dialog (NscConverter *converter)
{
	NscConverterPrivate *priv;
	GtkWidget           *hbox;
	gboolean             result;

	priv = NSC_CONVERTER_GET_PRIVATE (converter);

	/* Create the gtkbuilder and grab some widgets */
	result = nsc_xml_get_file ("main.ui",
				   "main_dialog", &priv->dialog,
				   "path_chooser", &priv->path_chooser,
				   "format_hbox", &hbox,
				   NULL);

	if (!result) {
		return;
	}

	/*
	 * Set the source directory if the user wants
	 * to use that as the output destination.
	 */
	if (priv->src_dir) {
		CajaFileInfo *file_info;
		gchar            *uri;

		file_info = CAJA_FILE_INFO (priv->files->data);
		uri = caja_file_info_get_uri (file_info);

		gtk_file_chooser_set_uri (GTK_FILE_CHOOSER (priv->path_chooser),
					  uri);
		g_free (uri);
	}

	/* Create the gstreamer audio profile chooser */
	priv->profile_chooser = nsc_audio_profile_chooser_new();

	/* Set which profile is active */
	if (priv->profile) {
		gchar *media_type;
		media_type = rb_gst_encoding_profile_get_media_type (priv->profile);
		nsc_audio_profile_chooser_set_active (priv->profile_chooser, media_type);
		g_free (media_type);
	}

	/* Let's pack the audio profile chooser */
	gtk_box_pack_start (GTK_BOX (hbox), priv->profile_chooser,
			    FALSE, FALSE, 0);

	/* Connect signals */
	g_signal_connect (G_OBJECT (priv->dialog), "response",
			  (GCallback) converter_response_cb,
			  converter);

	gtk_widget_show_all (priv->dialog);
}

static void
nsc_converter_init (NscConverter *self)
{
	/* Allocate private data structure */
	(NSC_CONVERTER (self))->priv = \
		(NscConverterPrivate *) g_malloc0 (sizeof (NscConverterPrivate));

	/* If correctly allocated, initialize parameters */
	if ((NSC_CONVERTER (self))->priv != NULL) {
		NscConverterPrivate *priv = NSC_CONVERTER_GET_PRIVATE (self);
		GSettings           *gsettings;

		/* Set init values */
		priv->gst = NULL;
		priv->files_converted = 0;
		priv->current_duration = 0;
		priv->total_duration = 0;
		priv->before.seconds = -1;

		/* Get GSettings client */
		gsettings = g_settings_new (SOURCE_DIRECTORY);
		if (gsettings == NULL) {
			/* Should probably do more than just give a warning */
			g_warning (_("Could not create GSettings client.\n"));
		}

		priv->src_dir = g_settings_get_boolean (gsettings, "source-dir");

		/* Unreference the gsettings client */
		g_object_unref (gsettings);

		/* Set the profile to the default. */
		priv->profile = rb_gst_get_encoding_profile (DEFAULT_MEDIA_TYPE);
	}
}

/*
 * Public Methods
 */
NscConverter *
nsc_converter_new (GList *files)
{
	return g_object_new (NSC_TYPE_CONVERTER, "files", files, NULL);
}

void
nsc_converter_show_dialog (NscConverter *converter)
{
	g_return_if_fail (NSC_IS_CONVERTER (converter));

	create_main_dialog (converter);
}
