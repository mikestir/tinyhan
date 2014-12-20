/*
 * tinyapp.h
 *
 *  Created on: 6 Dec 2014
 *      Author: mike
 */

#ifndef TINYAPP_H_
#define TINYAPP_H_

#include <stdint.h>

#define TINYAPP_FLAGS_NONE			0x00			/*< Flags field is RFU */

#define TINYAPP_ITEM_ID_MASK		0x3f			/*< Mask for profile data ID */
#define TINYAPP_ITEM_ID_PROFILE		0x3f			/*< Special value used to switch profiles */
#define TINYAPP_LENGTH_1			(0 << 6)
#define TINYAPP_LENGTH_2			(1 << 6)
#define TINYAPP_LENGTH_4			(2 << 6)
#define TINYAPP_LENGTH_VAR			(3 << 6)
#define TINYAPP_LENGTH_MASK			(3 << 6)

/*!
 * Raw data type on the air interface.  Note that processing of integer values
 * at higher levels may convert to floating point by way of fixed->float conversion
 * factors defined in the schema.
 */
typedef enum {
	tinyappType_Unsigned = 0,				/*< Little-endian unsigned integer */
	tinyappType_Signed,						/*< Little-endian signed integer */
	tinyappType_SingleFloat,				/*< IEEE754 float in 4 byte fixed length field */
	tinyappType_DoubleFloat,				/*< IEEE754 double in 8 byte variable length field */
	tinyappType_String,						/*< Variable length UTF-8 string */
	tinyappType_Binary,						/*< Unspecified variable length binary */
} tinyapp_type_t;

typedef struct {
	const char 		*tag;
	tinyapp_type_t	type;
} tinyapp_item_info_t;

typedef enum {
	tinyappProfile_System = 0x00,			/*< System/default */
	tinyappProfile_Generic,					/*< Generic data */
	tinyappProfile_Environmental,			/*< Environmental data */
	tinyappProfile_HVAC,					/*< Heating, Ventilation and Air Conditioning */
	tinyappProfile_Energy,					/*< Energy monitoring and management */
	tinyappProfile_HA,						/*< Home/building automation */
} tinyapp_profile_t;

/*! Node types, for user interface informational purposes */
typedef enum {
	tinyappNodeType_Generic = 0,			/*< Unspecified node type */
	tinyappNodeType_Sensor,					/*< General purpose environmental sensor */
	tinyappNodeType_Heating,				/*< Heating control */
	tinyappNodeType_Occupancy,				/*< Occupancy detection node */
	tinyappNodeType_Lighting,				/*< Lighting controller */
	tinyappNodeType_Switch,					/*< Switch or dimmer */
} tinyapp_nodetype_t;

typedef enum {
	tinyappSystem_NodeType = 0x00,			/*< Node type */
	tinyappSystem_Name,						/*< Friendly name (variable length) */
	tinyappSystem_Battery,					/*< Battery voltage (mV) */
	tinyappSystem_UpdateRate,				/*< Data update rate (seconds) */
	tinyappSystem_Runtime,					/*< Total runtime (seconds) */
} tinyapp_profile_system_t;

typedef enum {
	tinyappGeneric_Analogue0 = 0x00,		/*< General purpose analogue value 0 */
	tinyappGeneric_Analogue1,				/*< General purpose analogue value 1 */
	tinyappGeneric_Analogue2,				/*< General purpose analogue value 2 */
	tinyappGeneric_Analogue3,				/*< General purpose analogue value 3 */
	tinyappGeneric_Analogue4,				/*< General purpose analogue value 4 */
	tinyappGeneric_Analogue5,				/*< General purpose analogue value 5 */
	tinyappGeneric_Analogue6,				/*< General purpose analogue value 6 */
	tinyappGeneric_Analogue7,				/*< General purpose analogue value 7 */
	tinyappGeneric_DigitalIn,				/*< Digital inputs (bit mask) */
	tinyappGeneric_DigitalOut,				/*< Digital outputs (bit mask) */
	tinyappGeneric_FloatTest,
	tinyappGeneric_DoubleTest,
} tinyapp_profile_generic_t;

typedef enum {
	tinyappEnvironmental_Temperature = 0x00,	/*< Temperature (degrees C / 100) */
	tinyappEnvironmental_RelativeHumidity,		/*< Relative humidity % */
	tinyappEnvironmental_IlluminanceVisible,
	tinyappEnvironmental_IlluminanceUV,
	tinyappEnvironmental_IlluminanceIR,
	tinyappEnvironmental_pH,
	tinyappEnvironmental_FluidLevel,
	tinyappEnvironmental_WindSpeedAverage,
	tinyappEnvironmental_WindSpeedGust,
	tinyappEnvironmental_WindDirection,
	tinyappEnvironmental_Rainfall,
	tinyappEnvironmental_AtmosphericPressure,
	tinyappEnvironmental_SPLRMS,
	tinyappEnvironmental_SPLPeak,
} tinyapp_profile_environmental_t;

typedef enum {
	tinyappHVAC_RoomTemperature = 0x00,
	tinyappHVAC_TargetTemperature,
	tinyappHVAC_HeatingDemand,
	tinyappHVAC_CoolingDemand,
	tinyappHVAC_FanSpeed,
	tinyappHVAC_FlowTemperature,
	tinyappHVAC_ReturnTemperature,
	tinyappHVAC_BurnerPower,
} tinyapp_profile_hvac_t;

#if 0
typedef enum {

} tinyapp_profile_energy_t;

typedef enum {

} tinyapp_profile_ha_t;
#endif


typedef struct {
	uint8_t				*packet_ptr;
	uint8_t				*packet_start;
	uint8_t				*packet_end;
} tinyapp_t;

/* Serialising */
void tinyapp_init(tinyapp_t *ctx, uint8_t flags, uint8_t *buf, size_t size);
void tinyapp_serialise8(tinyapp_t *ctx, uint8_t type, uint8_t value);
void tinyapp_serialise16(tinyapp_t *ctx, uint8_t type, uint16_t value);
void tinyapp_serialise32(tinyapp_t *ctx, uint8_t type, uint32_t value);
void tinyapp_serialise_string(tinyapp_t *ctx, uint8_t type, const char *str, size_t len);

static inline size_t tinyapp_size(tinyapp_t *ctx)
{
	return (size_t)(ctx->packet_ptr - ctx->packet_start);
}

/* De-serialising */
typedef void(*tinyapp_item_cb_t)(uint8_t profile, uint8_t item, const void *value, size_t len);
void tinyapp_deserialise(const uint8_t *buf, size_t size, tinyapp_item_cb_t cb);

/* Utility functions for profile/item conversion and name tables */
int tinyapp_get_profile_id(const char *tag);
const char* tinyapp_get_profile_tag(uint8_t profile);
int tinyapp_get_item_id(uint8_t profile, const char *tag);
const tinyapp_item_info_t* tinyapp_get_item_info(uint8_t profile, uint8_t item);

#endif /* TINYAPP_H_ */
