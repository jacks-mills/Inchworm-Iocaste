# [Inchworm](https://www.colorhexa.com/b2ec5d)-[Iocaste](https://en.wikipedia.org/wiki/Iocaste_(moon)): Stopping a Train before you can say "Oh no there is an obstacle ahead I need to stop the train!"
Authours:
* Ethan Ney
* Jack Mills


Draft readme to get the ball rolling.



## META
A section for writing things about the project. Won't necessarily be included in the final release.

### GIT WORKFLOW
The [git workflow](https://simondosda.github.io/posts/2022-01-03-git-rebase-workflow.html) we aim to
use.


### TODO
[x] 2026/05/09 ~~Write a draft readme.~~




## PROJECT OVERVIEW
Topic: F) Embedded AI Board Topics

Our topic is to develope and automatic breaking system for trains.
* Conductors cannot see far enough to see obstacles in time to break and avoid a collision
* Automated system *can* see far enough, and break earlier, justifying the project
* Trains already communicate their state back to a central location, makes sense to also communicate
  break status back to central, and to engage the break remotely if necessary

IDEA
* perhaps also have an LED for *I (the system) see something and want to let you know, so you can
  pull the lever yourself, but if you don't I will pull the lever*, i.e. if the obstacle has been
  detected but theres still time to break, don't break yet and notify operator


### SENSORS
* Camera
* rotary sensor at pivot to detect angle?
* linear sensor to measure actuator extension?

### ACTUATORS
* linear actuator (for automatically pulling break/lever)

### WIRELESS NETWORKING
* between break and base station

### ALGORITHMS
* Yolo machine learning (should be more specific, about why version of Yolo and other such details)

### WEB DASHBOARD
* Display break state
* Display detection state
* Control break remotely

### DEVICES (not in rubric but useful)
* Jetson (break)
* Xiao ble (base station)


## KEY PERFORMANCE INDICATORS
[ ] Detects person straight-on
[ ] Detects person on the margins
[ ] LED notifies obstacle detection
[ ] Handbreak automatically closes when breaking
[ ] Break state communicated to base station
[ ] Display state on dashboard
[ ] Control break remotely from base state



## TEAM MEMBER LIST AND ROLES
Ethan Ney

Jack Mills:
* Create README (example of a task/role)
