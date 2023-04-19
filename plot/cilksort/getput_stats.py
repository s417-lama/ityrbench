import os
import sys
import pathlib
import itertools
import math
import numpy as np
import pandas as pd
import plotly.graph_objects as go
import plotly.io as pio

os.chdir(pathlib.Path(__file__).parent.parent.parent)
sys.path.append("./plot")
import plot_util

benchmark = "cilksort"

machine = "wisteria-o"

fig_dir = os.path.join("figs", benchmark)

event_looks = dict(
    others=dict(
        rank=9,
        pattern="",
        solidity=0.5,
        color="#444444",
        title="Others",
    ),
    get=dict(
        rank=8,
        pattern="\\",
        solidity=0.5,
        color=plot_util.tol_cset("light").light_blue,
        title="Get",
    ),
    put=dict(
        rank=7,
        pattern="/",
        solidity=0.5,
        color=plot_util.tol_cset("light").orange,
        title="Put",
    ),
    checkout=dict(
        rank=6,
        pattern="|",
        solidity=0.5,
        color=plot_util.tol_cset("light").light_cyan,
        title="Checkout",
    ),
    checkin=dict(
        rank=5,
        pattern="-",
        solidity=0.5,
        color=plot_util.tol_cset("light").pink,
        title="Checkin",
    ),
    release=dict(
        rank=4,
        pattern=".",
        solidity=0.4,
        color=plot_util.tol_cset("light").pear,
        title="Release",
    ),
    release_lazy=dict(
        rank=3,
        pattern=".",
        solidity=0.7,
        color=plot_util.tol_cset("light").olive,
        title="Lazy Release",
    ),
    acquire=dict(
        rank=2,
        pattern="+",
        solidity=0.6,
        color=plot_util.tol_cset("light").mint,
        title="Acquire",
    ),
    merge_kernel=dict(
        rank=1,
        pattern="x",
        solidity=0.7,
        color="#BBBBBB",
        title="Serial Merge",
    ),
    quicksort_kernel=dict(
        rank=0,
        pattern="",
        solidity=0.5,
        color="#BBBBBB",
        title="Serial Quicksort",
    ),
)

pio.templates.default = "plotly_white"

n_warmups = 1

def get_result(batch_name, filename_template, **args):
    files = [(os.path.join(machine, benchmark, batch_name, filename_template.format(**dict(p))), dict(p))
             for p in itertools.product(*[[(k, v) for v in args[k]] for k in args])]
    df = plot_util.txt2df(files, [
            r'# of processes: *(?P<nproc>\d+)',
            r'\[(?P<i>\d+)\] *(?P<time>\d+) *ns',
        ], [
            r'^ *(?P<event>[a-zA-Z_]+) .*\( *(?P<acc>\d+) *ns */.*\)',
        ])
    df = df.loc[df["i"] >= n_warmups].copy()
    return df

def get_parallel_result_shmem():
    return get_result("getput_shmem", "c_{cache_policy}_d_{dist_policy}_{duplicate}.out",
                      label=["shmem"],
                      cache_policy=["nocache", "writeback_lazy", "getput"],
                      dist_policy=["block", "cyclic"],
                      nodes=[1],
                      duplicate=[0, 1, 2])

def get_parallel_result_multinode():
    if machine == "wisteria-o":
        nodes = ["3x4x3:torus"]
    return get_result("getput_multinode", "c_{cache_policy}_d_{dist_policy}_{duplicate}.out",
                      label=["multinode"],
                      cache_policy=["nocache", "writeback_lazy", "getput"],
                      dist_policy=["block", "cyclic"],
                      nodes=nodes,
                      duplicate=[0, 1, 2])

if __name__ == "__main__":
    fig = go.Figure()

    df = pd.concat([get_parallel_result_shmem(), get_parallel_result_multinode()])
    df = df[df["event"].isin(event_looks.keys())]
    df["acc"] /= df["nproc"]
    df = df.groupby(["label", "cache_policy", "dist_policy", "event"]).agg({"acc": "mean", "time": "max"})
    df = df.reset_index()

    for (label, cache_policy, dist_policy), df_nproc in df.groupby(["label", "cache_policy", "dist_policy"]):
        df = pd.concat([df, pd.DataFrame.from_records([
            {"label": label, "cache_policy": cache_policy, "dist_policy": dist_policy,
             "event": "others",
             "acc": df_nproc["time"].max() - df_nproc["acc"].sum()}])])

    print(df)

    plot_titles = {
        ("shmem"    , "getput"        , "block" ): "GET/PUT<br>1 node",
        ("shmem"    , "writeback_lazy", "block" ): "Checkout/in<br>1 node",
        ("multinode", "getput"        , "cyclic"): "GET/PUT<br>36 nodes",
        ("multinode", "writeback_lazy", "cyclic"): "Checkout/in<br>36 nodes",
    }

    for event, looks in reversed(event_looks.items()):
        df_event = df[(df["event"] == event) &
                      (df.set_index(["label", "cache_policy", "dist_policy"]).index.isin(plot_titles.keys()))]
        df_event = df_event.assign(title=df_event.apply(lambda x: plot_titles[(x["label"], x["cache_policy"], x["dist_policy"])], axis=1))
        fig.add_trace(go.Bar(
            x=df_event["title"],
            y=df_event["acc"] / 1_000_000_000,
            legendrank=looks["rank"],
            marker_color=looks["color"],
            marker_line_color="#333333",
            marker_line_width=1.5,
            marker_pattern_fillmode="replace",
            marker_pattern_solidity=looks["solidity"],
            marker_pattern_shape=looks["pattern"],
            marker_pattern_size=8,
            name=looks["title"],
        ))

    # annotation
    df = df[(df.set_index(["label", "cache_policy", "dist_policy"]).index.isin(plot_titles.keys()))]
    df = df.groupby(["label", "cache_policy", "dist_policy"]).agg({"time": "max"})
    df = df.reset_index()
    df = df.assign(title=df.apply(lambda x: plot_titles[(x["label"], x["cache_policy"], x["dist_policy"])], axis=1))
    fig.add_trace(go.Scatter(
        x=df["title"],
        y=df["time"] / 1_000_000_000,
        texttemplate="%{y:.2f} s",
        mode="text",
        textposition="top center",
        showlegend=False,
        textfont_size=18,
    ))

    fig.update_xaxes(
        showline=True,
        linecolor="black",
        mirror=True,
        title_standoff=10,
    )
    fig.update_yaxes(
        range=[0,6.7],
        showline=True,
        linecolor="black",
        ticks="outside",
        mirror=True,
        showgrid=True,
        title="Execution Time (s)",
    )
    fig.update_layout(
        width=550,
        height=280,
        margin=dict(l=0, r=0, b=0, t=0),
        barmode="stack",
        legend_font_size=15,
        font=dict(
            family="Linux Biolinum O, sans-serif",
            size=16,
        ),
    )

    plot_util.save_fig(fig, fig_dir, "getput_stats_{}.html".format(machine))
