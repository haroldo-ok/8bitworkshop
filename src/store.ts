"use strict";

// TODO: use modules; export type for LocalForageDbMethods
//import * as localforage from "localforage";
declare var localforage;

var Ver2xFileStore = function(storage, prefix:string) {
  var self = this;
  this.saveFile = function(name, text) {
    storage.setItem(prefix + name, text);
  }
  this.loadFile = function(name) {
    return storage.getItem(prefix + name) || storage.getItem(name);
  }
  this.getFiles = function(prefix2) {
    // iterate over files with <platform>/<dir> prefix
    var files = [];
    for (var i = 0; i < storage.length; i++) {
      var key = storage.key(i);
      if (key.startsWith(prefix + prefix2)) {
        var name = key.substring(prefix.length + prefix2.length);
        files.push(name);
      }
    }
    return files;
  }
  this.deleteFile = function(name) {
    storage.removeItem(prefix + name);
    storage.removeItem(prefix + 'local/' + name); //TODO?
  }
}

// copy localStorage to new driver
function copyFromVer2xStorageFormat(storeid:string, newstore, callback:(store)=>void) {
  var alreadyMigratedKey = "__migrated_" + storeid;
  //localStorage.removeItem(alreadyMigratedKey);
  if (localStorage.getItem(alreadyMigratedKey)) {
    callback(newstore);
    return;
  }
  var oldstore = new Ver2xFileStore(localStorage, storeid + '/');
  var keys = oldstore.getFiles('');
  // no files to convert?
  if (keys.length == 0) {
    localStorage.setItem(alreadyMigratedKey, 'true');
    callback(newstore);
    return;
  }
  // convert function
  function migrateNext() {
    var key = keys.shift();
    var value = oldstore.loadFile(key);
    newstore.setItem(key, value, function(err, result) {
      if (err) {
        console.log(err);
      } else {
        console.log("Converted " + key);
        if (keys.length) {
          migrateNext();
        } else {
          newstore.length(function(err, len) {
            if (err) throw err;
            console.log("Migrated " + len + " local files to new data store");
            if (len) {
              localStorage.setItem(alreadyMigratedKey, 'true');
              callback(newstore);
            }
          });
        }
      }
    });
  }
  migrateNext(); // start the conversion
}

export function createNewPersistentStore(storeid:string, callback:(store)=>void) {
  var store = localforage.createInstance({
    name: "__" + storeid,
    version: 2.0
  });
  copyFromVer2xStorageFormat(storeid, store, callback);
  return store;
}
