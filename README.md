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

## secure connection parameters

`db_sslmode` ssl mode 

`sslrootcert` path to ssl root cert file 

`sslcert` path to ssl cert file for client

`sslkey` path to ssl key file for client (must match the database user)
