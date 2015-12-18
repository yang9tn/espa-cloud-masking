
/*****************************************************************************
*****************************************************************************/

#include <time.h>


#include "espa_geoloc.h"
#include "raw_binary_io.h"


#include "const.h"
#include "error.h"
#include "input.h"
#include "output.h"


#define FMASK_PRODUCT "cfmask"
#define FMASK_SHORTNAME "CFMASK"
#define FMASK_NAME "cfmask"
#define FMASK_LONG_NAME "cfmask_band"
#define FMASK_CONFIDENCE_SHORTNAME "CFMASK_CONF"
#define FMASK_CONFIDENCE_NAME "cfmask_conf"
#define FMASK_CONFIDENCE_LONG_NAME "cfmask_conf_band"


/*****************************************************************************
MODULE:  OpenOutput

PURPOSE: Sets up the Output_t data structure and opens the output file for
         write access.
 
RETURN: Type = Output_t *
    A populated Output_t data structure or NULL when an error occurs

NOTES:
    MASK_INDEX "0 clear; 1 water; 2 cloud_shadow; 3 snow; 4 cloud"
*****************************************************************************/
Output_t *OpenOutput
(
    Espa_internal_meta_t *in_meta, /* I: input metadata structure */
    Input_t *input                 /* I: input reflectance band data */
)
{
    Output_t *output = NULL;
    char *mychar = NULL;        /* pointer to '_' */
    char scene_name[STR_SIZE];  /* scene name for the current scene */
    char file_name[STR_SIZE];   /* output filename */
    char production_date[MAX_DATE_LEN + 1]; /* current date/time for
                                               production */
    time_t tp;                  /* time structure */
    struct tm *tm = NULL;       /* time structure for UTC time */
    int ib;                     /* looping variable for bands */
    int ref_index = -1;         /* band index in XML file for the reflectance
                                   band */
    Espa_band_meta_t *bmeta = NULL; /* pointer to the band metadata array
                                       within the output structure */

    /* Create the Output data structure */
    output = malloc(sizeof(Output_t));
    if (output == NULL)
        RETURN_ERROR("allocating Output data structure", "OpenOutput", NULL);

    /* Find the representative band for metadata information */
    for (ib = 0; ib < in_meta->nbands; ib++)
    {
        if (!strcmp(in_meta->band[ib].name, "toa_band1") &&
            !strcmp(in_meta->band[ib].product, "toa_refl"))
        {
            /* this is the index we'll use for reflectance band info */
            ref_index = ib;
            break;
        }
    }

    /* Make sure we found the TOA band 1 */
    if (ref_index == -1)
    {
        RETURN_ERROR("Unable to find the TOA reflectance bands in the XML file"
                     " for initializing the output metadata.",
                     "OpenOutput", NULL);
    }

    /* Initialize the internal metadata for the output product. The global
       metadata won't be updated, however the band metadata will be updated
       and used later for appending to the original XML file. */
    init_metadata_struct(&output->metadata);

    /* Allocate memory for the output band */
    if (allocate_band_metadata(&output->metadata, 1) != SUCCESS)
    {
        RETURN_ERROR("allocating band metadata", "OpenOutput", NULL);
    }
    bmeta = output->metadata.band;

    /* Determine the scene name */
    strcpy(scene_name, in_meta->band[ref_index].file_name);
    mychar = strchr(scene_name, '_');
    if (mychar != NULL)
        *mychar = '\0';

    /* Get the current date/time (UTC) for the production date of each band */
    if (time(&tp) == -1)
        RETURN_ERROR("unable to obtain current time", "OpenOutput", NULL);

    tm = gmtime(&tp);
    if (tm == NULL)
    {
        RETURN_ERROR("converting time to UTC", "OpenOutput", NULL);
    }

    if (strftime(production_date, MAX_DATE_LEN, "%Y-%m-%dT%H:%M:%SZ", tm) == 0)
    {
        RETURN_ERROR("formatting the production date/time", "OpenOutput", NULL);
    }

    /* Populate the data structure */
    output->open = false;
    output->fp_bin = NULL;
    output->nband = 1;
    output->size.l = input->size.l;
    output->size.s = input->size.s;

    strncpy(bmeta[0].short_name, in_meta->band[ref_index].short_name, 3);
    bmeta[0].short_name[3] = '\0';
    strcat(bmeta[0].short_name, FMASK_SHORTNAME);
    strcpy(bmeta[0].product, FMASK_PRODUCT);
    strcpy(bmeta[0].source, "toa_refl");
    strcpy(bmeta[0].category, "qa");
    bmeta[0].nlines = output->size.l;
    bmeta[0].nsamps = output->size.s;
    bmeta[0].pixel_size[0] = input->meta.pixel_size[0];
    bmeta[0].pixel_size[1] = input->meta.pixel_size[1];
    strcpy(bmeta[0].pixel_units, "meters");
    sprintf(bmeta[0].app_version, "%s_%s", CFMASK_APP_NAME, CFMASK_VERSION);
    strcpy(bmeta[0].production_date, production_date);
    bmeta[0].data_type = ESPA_UINT8;
    bmeta[0].fill_value = CF_FILL_PIXEL;
    bmeta[0].valid_range[0] = 0;
    bmeta[0].valid_range[1] = 4;
    strcpy(bmeta[0].name, FMASK_NAME);
    strcpy(bmeta[0].long_name, FMASK_LONG_NAME);
    strcpy(bmeta[0].data_units, "quality/feature classification");

    /* Set up class values information */
    if (allocate_class_metadata(&bmeta[0], 6) != SUCCESS)
    {
        RETURN_ERROR ("allocating cfmask classes", "OpenOutput", NULL);
    }

    /* Identify the class values for the mask */
    bmeta[0].class_values[0].class = 0;
    bmeta[0].class_values[1].class = 1;
    bmeta[0].class_values[2].class = 2;
    bmeta[0].class_values[3].class = 3;
    bmeta[0].class_values[4].class = 4;
    bmeta[0].class_values[5].class = CF_FILL_PIXEL;
    strcpy(bmeta[0].class_values[0].description, "clear");
    strcpy(bmeta[0].class_values[1].description, "water");
    strcpy(bmeta[0].class_values[2].description, "cloud_shadow");
    strcpy(bmeta[0].class_values[3].description, "snow");
    strcpy(bmeta[0].class_values[4].description, "cloud");
    strcpy(bmeta[0].class_values[5].description, "fill");

    /* Set up the filename with the scene name and band name and open the
       file for write access */
    sprintf(file_name, "%s_%s.img", scene_name, bmeta[0].name);
    strcpy(bmeta[0].file_name, file_name);
    output->fp_bin = open_raw_binary(file_name, "w");
    if (output->fp_bin == NULL)
    {
        RETURN_ERROR("unable to open output file", "OpenOutput", NULL);
    }
    output->open = true;

    return output;
}


Output_t *OpenOutputConfidence
(
    Espa_internal_meta_t *in_meta, /* I: input metadata structure */
    Input_t *input                 /* I: input reflectance band data */
)
{
    Output_t *output = NULL;
    char *mychar = NULL;        /* pointer to '_' */
    char scene_name[STR_SIZE];  /* scene name for the current scene */
    char file_name[STR_SIZE];   /* output filename */
    char production_date[MAX_DATE_LEN + 1]; /* current date/time for
                                               production */
    time_t tp;                  /* time structure */
    struct tm *tm = NULL;       /* time structure for UTC time */
    int ib;                     /* looping variable for bands */
    int ref_index = -1;         /* band index in XML file for the reflectance
                                   band */
    Espa_band_meta_t *bmeta = NULL; /* pointer to the band metadata array
                                       within the output structure */

    /* Create the Output data structure */
    output = malloc(sizeof(Output_t));
    if (output == NULL)
        RETURN_ERROR ("allocating Output data structure", "OpenOutput", NULL);

    /* Find the representative band for metadata information */
    for (ib = 0; ib < in_meta->nbands; ib++)
    {
        if (!strcmp(in_meta->band[ib].name, "toa_band1") &&
            !strcmp(in_meta->band[ib].product, "toa_refl"))
        {
            /* this is the index we'll use for reflectance band info */
            ref_index = ib;
            break;
        }
    }

    /* Make sure we found the TOA band 1 */
    if (ref_index == -1)
    {
        RETURN_ERROR("Unable to find the TOA reflectance bands in the XML file"
                     " for initializing the output metadata.",
                     "OpenOutput", NULL);
    }

    /* Initialize the internal metadata for the output product. The global
       metadata won't be updated, however the band metadata will be updated
       and used later for appending to the original XML file. */
    init_metadata_struct(&output->metadata);

    /* Allocate memory for the output band */
    if (allocate_band_metadata(&output->metadata, 1) != SUCCESS)
    {
        RETURN_ERROR("allocating band metadata", "OpenOutput", NULL);
    }
    bmeta = output->metadata.band;

    /* Determine the scene name */
    strcpy(scene_name, in_meta->band[ref_index].file_name);
    mychar = strchr(scene_name, '_');
    if (mychar != NULL)
        *mychar = '\0';

    /* Get the current date/time (UTC) for the production date of each band */
    if (time (&tp) == -1)
        RETURN_ERROR("unable to obtain current time", "OpenOutput", NULL);

    tm = gmtime (&tp);
    if (tm == NULL)
        RETURN_ERROR("converting time to UTC", "OpenOutput", NULL);

    if (strftime(production_date, MAX_DATE_LEN, "%Y-%m-%dT%H:%M:%SZ", tm) == 0)
    {
        RETURN_ERROR("formatting the production date/time", "OpenOutput", NULL);
    }

    /* Populate the data structure */
    output->open = false;
    output->fp_bin = NULL;
    output->nband = 1;
    output->size.l = input->size.l;
    output->size.s = input->size.s;

    strncpy(bmeta[0].short_name, in_meta->band[ref_index].short_name, 3);
    bmeta[0].short_name[3] = '\0';
    strcat(bmeta[0].short_name, FMASK_CONFIDENCE_SHORTNAME);
    strcpy(bmeta[0].product, FMASK_PRODUCT);
    strcpy(bmeta[0].source, "toa_refl");
    strcpy(bmeta[0].category, "qa");
    bmeta[0].nlines = output->size.l;
    bmeta[0].nsamps = output->size.s;
    bmeta[0].pixel_size[0] = input->meta.pixel_size[0];
    bmeta[0].pixel_size[1] = input->meta.pixel_size[1];
    strcpy(bmeta[0].pixel_units, "meters");
    sprintf(bmeta[0].app_version, "%s_%s", CFMASK_APP_NAME, CFMASK_VERSION);
    strcpy(bmeta[0].production_date, production_date);
    bmeta[0].data_type = ESPA_UINT8;
    bmeta[0].fill_value = CF_FILL_PIXEL;
    bmeta[0].valid_range[0] = 0;
    bmeta[0].valid_range[1] = 3;
    strcpy(bmeta[0].name, FMASK_CONFIDENCE_NAME);
    strcpy(bmeta[0].long_name, FMASK_CONFIDENCE_LONG_NAME);
    strcpy(bmeta[0].data_units, "quality/feature classification");

    /* Set up class values information */
    if (allocate_class_metadata(&bmeta[0], 5) != SUCCESS)
    {
        RETURN_ERROR("allocating cfmask classes", "OpenOutput", NULL);
    }

    /* Identify the class values for the mask */
    bmeta[0].class_values[0].class = 0;
    bmeta[0].class_values[1].class = 1;
    bmeta[0].class_values[2].class = 2;
    bmeta[0].class_values[3].class = 3;
    bmeta[0].class_values[4].class = CF_FILL_PIXEL;
    strcpy(bmeta[0].class_values[0].description, "None");
    strcpy(bmeta[0].class_values[1].description,
           "less than or equal to 12.5 Percent Cloud Confidence");
    strcpy(bmeta[0].class_values[2].description,
           "greater than 12.5 and less than or equal to 22.5"
           " Percent Cloud Confidence");
    strcpy(bmeta[0].class_values[3].description,
           "greater than 22.5 Percent Cloud Confidence");
    strcpy(bmeta[0].class_values[4].description, "fill");

    /* Set up the filename with the scene name and band name and open the
       file for write access */
    sprintf(file_name, "%s_%s.img", scene_name, bmeta[0].name);
    strcpy(bmeta[0].file_name, file_name);
    output->fp_bin = open_raw_binary(file_name, "w");
    if (output->fp_bin == NULL)
    {
        RETURN_ERROR("unable to open output file", "OpenOutput", NULL);
    }
    output->open = true;

    return output;
}


/*****************************************************************************
MODULE:  CloseOutput

PURPOSE: Ends access and closes the output files.

RETURN:  Type = Bool,  Updated Output_T data structure.
    Output_t: The following field is modified: open
    Value  Description
    -----  -------------------------------------------------------------------
    true   No Errors
    false  Errors encountered
*****************************************************************************/
bool
CloseOutput(Output_t *output)
{
    if (!output->open)
        RETURN_ERROR("image files not open", "CloseOutput", false);

    close_raw_binary(output->fp_bin);
    output->open = false;

    return true;
}


/*****************************************************************************
MODULE:  FreeOutput

PURPOSE: Frees the Output_t data structure memory.

RETURN:  Type = Bool,  Updated Output_T data structure.
    Output_t:  Memory fields reset to NULL
    Value  Description
    -----  -------------------------------------------------------------------
    true   No Errors
    false  Errors encountered
*****************************************************************************/
bool
FreeOutput(Output_t *output)
{
    if (output->open)
        RETURN_ERROR("file still open", "FreeOutput", false);

    free(output);
    output = NULL;

    return true;
}


/*****************************************************************************
MODULE:  PutOutputLine

PURPOSE: Writes nlines of data to the output file.

RETURN:  Type = Bool
    Value  Description
    -----  -------------------------------------------------------------------
    true   No Errors
    false  Errors encountered
*****************************************************************************/
bool
PutOutput(Output_t *output, unsigned char *final_mask)
{
    /* Check the parameters */
    if (output == NULL)
        RETURN_ERROR("invalid input structure", "PutOutputLine", false);
    if (!output->open)
        RETURN_ERROR("file not open", "PutOutputLine", false);

    if (write_raw_binary(output->fp_bin, output->size.l, output->size.s,
                         sizeof(unsigned char), final_mask) != SUCCESS)
    {
        RETURN_ERROR("writing output line", "PutOutput", false);
    }

    return true;
}