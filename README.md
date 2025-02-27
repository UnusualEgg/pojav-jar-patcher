# (PojavLauncher) Jar Patcher
## what is 
Creates a version folder for pojavlauncher. Can also be used to just patch a jar file
# order.json
## what is
- mc
  - in (string)
    input minecraft jar file
  - out (string)
    name of output file (no extension). makes a zip, jar, and version json
- mods (array of strings)
  searches inside the "input" directory for these files. For each one, looks in it to see if it has a mod_* file. If so, then it's a modloader mod and goes into the mods folder. else, it goes into the jar
- in_jar (array of strings)
  like mods but unconditionally puts it into the jar
## example
```
{
  "mc": {
    "in": "client.jar",
    "out": "modded_jar"
  },
  "mods": [
    "example_modloader_mod.jar"
  ],
  "in_jar": [
    "example_mod.jar" 
  ]
}
```
