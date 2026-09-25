#ifndef CES_C_SYSTEM_H_
#define CES_C_SYSTEM_H_

#include "ces/c/types.h"

#ifdef __cplusplus
extern "C" {
#endif

CesResult CesInit(void);
void CesShutdown(void);

// Catalog. Name is ERR_* (with or without prefix). 0 if unknown.
int CesErrorCode(const char* name);
CesResult CesErrorName(int code, char* buf, uint32_t len);
int CesIsValid(int code);
uint32_t CesCatalogCount(void);
CesResult CesCatalogAt(uint32_t i, char* buf, uint32_t len);
uint32_t CesInterstitialCount(void);
CesResult CesInterstitialAt(uint32_t i, char* buf, uint32_t len);
uint32_t CesDeputyCount(void);
CesResult CesDeputyAt(uint32_t i, char* buf, uint32_t len);
uint32_t CesOccupyCount(void);
CesResult CesOccupyAt(uint32_t i, char* buf, uint32_t len);
uint32_t CesDriveCount(void);
CesResult CesDriveAt(uint32_t i, char* buf, uint32_t len);
CesResult CesDump(char* buf, uint32_t len);

// Unique URLLoader. Resolves chrome://dino / chrome://network-error/<N>
// / chrome://interstitials/<id> into a Shot JSON. Never talks to Chrome.
CesResult CesResolve(const char* url, char* buf, uint32_t len);
CesResult CesFireWebdata(char* buf, uint32_t len);
CesResult CesFireDino(char* buf, uint32_t len);
CesResult CesFireCode(int code, char* buf, uint32_t len);
CesResult CesFireName(const char* name, char* buf, uint32_t len);
CesResult CesFireInterstitial(const char* id, char* buf, uint32_t len);
CesResult CesFireDeputy(const char* id, char* buf, uint32_t len);
CesResult CesLastShot(char* buf, uint32_t len);
CesResult CesLoadTimeData(char* buf, uint32_t len);

const char* CesCommittedOrigin(void);
int CesDinoNetError(void);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CES_C_SYSTEM_H_
