# [Inchworm](https://www.colorhexa.com/b2ec5d)-[Iocaste](https://en.wikipedia.org/wiki/Iocaste_(moon)): Stopping a Train Before You Can Say "Oh no there is an obstacle ahead and I need to stop the train!"

Authours:
* Ethan Ney
* Jack Mills

See [wiki](https://github.com/jacks-mills/Inchworm-Iocaste/wiki) for project details.

## Installation Insturctions

git clone the repo.

```
$ cd ~
$ git clone https://github.com/jacks-mills/Inchworm-Iocaste/
```

Activate the appropriate venv, and navigate to the brake controller directory, build the program.
```
$ cd ~/Inchworm-Iocaste/brake-controller/
$ west build -p -b xiao_ble/nrf52840/sense -d build/
```
Flash the program.
```
$ west flash --runner uf2 -d build/
```

Open a new terminal and begin the Flask application.
```
$ cd ~/Inchworm-Iocaste/pc
$ python3 app.py
```

Open your web browser and go to the URL `http://127.0.0.1:5000/`, from there the web dashboard should be visible, and the system can be controlled.
