# irods_database_plugin_cockroachdb

## prerequisite

build and install `irods_api_plugin_bulkreg_common` package

```
git clone https://github.com/xu-hao/irods_api_bulkreg_common
```

```
cd irods_api_bulkreg_common
```

```
mkdir build
```

```
cd build
```

```
cmake .. -GNinja
```

```
ninja package
```

```
sudo dpkg -i <package-name>
```

## build database plugin ##

```
git clone <this repo>
```

```
cd <repo_dir>
```

```
mkdir build
```

```
cd build
```

```
cmake .. -GNinja
```

```
ninja package
```
