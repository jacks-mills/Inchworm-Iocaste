#ifndef __BRAKE_H__
#define __BRAKE_H__


/* initialise brake and appropriate devices */
int brake_init(void);

/* set the desired brake percentage. returns old percentage */
int brake_set(int percentage);

/* reads actual brake percentage */
int brake_get(void);

#endif /* __BRAKE_H__ */
