#!/bin/bash

keymap parse -z config/Corne.keymap > corne.yaml

keymap draw corne.yaml -j config/Corne.json > corne.svg

gpicview corne.svg
