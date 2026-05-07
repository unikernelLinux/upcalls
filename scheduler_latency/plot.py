#!/usr/bin/python3
import seaborn as sns
import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import sys

def load_data():
    core = pd.read_csv("same_core.csv")
    socket = pd.read_csv("same_socket.csv")
    cross = pd.read_csv("cross_socket.csv")

    core["SETUP"] = "Same Core"
    socket["SETUP"] = "Same Socket"
    cross["SETUP"] = "Across Sockets"

    return pd.concat([core, socket, cross])

df = load_data()

fig = sns.boxplot(y='LATENCY_USEC', x='SETUP', data=df)
fig.set(title="Task switch latency")
plt.xlabel('Task Placement')
plt.ylabel('Latency (usec)')
plt.tight_layout()
plt.savefig('task_switch.pdf')
