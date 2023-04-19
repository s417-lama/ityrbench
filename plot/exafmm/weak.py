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

machine = "wisteria-o"

fig_dir = os.path.join("figs", "exafmm")

policy_looks = dict(
    nocache=dict(
        rank=0,
        color="#EE6677",
        dash="dot",
        title="No Cache",
        marker="circle-open",
    ),
    writethrough=dict(
        rank=1,
        color="#4477AA",
        dash="dash",
        title="Write-Through",
        marker="diamond-open",
    ),
    writeback=dict(
        rank=2,
        color="#CCBB44",
        dash="dashdot",
        title="Write-Back",
        marker="square-open",
    ),
    writeback_lazy=dict(
        rank=3,
        color="#228833",
        dash="solid",
        title="Write-Back (Lazy)",
        marker="star-triangle-up-open",
    ),
    mpi=dict(
        rank=4,
        color="#AA3377",
        dash="12px,3px,3px,3px,3px,3px",
        title="MPI+MassiveThreads",
        marker="x-thin",
    ),
)

pio.templates.default = "plotly_white"

linewidth = 2
itemwidth = 60
markersize = 12

n_warmups = 1

def get_result(benchmark, batch_name, filename_template, **args):
    files = [(os.path.join(machine, benchmark, batch_name, filename_template.format(**dict(p))), dict(p))
             for p in itertools.product(*[[(k, v) for v in args[k]] for k in args])]
    df = plot_util.txt2df(files, [
            r'# of processes *: *(?P<nproc>\d+)',
            r'-* *Time average loop (?P<i>\d+) *-*',
        ], [
            r'Traverse *: *(?P<time>\d+\.\d+) *s',
        ])
    df = df.loc[df["i"] >= n_warmups].copy()
    df["ncore"] = df["nproc"]
    return df

def get_parallel_result_1M():
    if machine == "wisteria-o":
        nodes = [1, "2:torus", "2x3:torus", "2x3x2:torus", "3x4x3:torus"]
        duplicates = [0, 1, 2]
    return get_result("exafmm", "weak1M", "nodes_{nodes}_p_{policy}_{duplicate}.out",
                      nodes=nodes,
                      policy=["nocache", "writethrough", "writeback", "writeback_lazy"],
                      duplicate=duplicates)

def get_mpi_result(benchmark, batch_name, filename_template, **args):
    files = [(os.path.join(machine, benchmark, batch_name, filename_template.format(**dict(p))), dict(p))
             for p in itertools.product(*[[(k, v) for v in args[k]] for k in args])]
    df = plot_util.txt2df(files, [
            r'threads *: *(?P<nthread>\d+)',
            r'# of processes *: *(?P<nproc>\d+)',
            r'-* *Time average loop (?P<i>\d+) *-*',
        ], [
            r'Traverse \(total\) *: *(?P<time>\d+\.\d+) *s',
        ])
    df = df.loc[df["i"] >= n_warmups].copy()
    df["ncore"] = df["nproc"] * df["nthread"]
    df["policy"] = "mpi"
    return df

def get_mpi_result_1M():
    if machine == "wisteria-o":
        nodes = [1, "2:torus", "2x3:torus", "2x3x2:torus", "3x4x3:torus"]
        duplicates = [0, 1, 2]
    return get_mpi_result("exafmm_mpi", "weak1M", "nodes_{nodes}_{duplicate}.out",
                          nodes=nodes,
                          duplicate=duplicates)

if __name__ == "__main__":
    fig = go.Figure()
    log_axis = dict(type="log", dtick=1, minor=dict(ticks="inside", ticklen=5, showgrid=True))
    yaxis_title = "Parallel Efficiency"
    x_axis = log_axis
    y_axis = dict()

    df_par = pd.concat([get_parallel_result_1M(), get_mpi_result_1M()])
    print(df_par)

    t_min = df_par["time"].min()

    for policy, df_p in df_par.groupby("policy"):
        print("## policy={}".format(policy))

        xs_all = []
        ys_all = []
        ci_uppers_all = []
        ci_lowers_all = []

        df_p = df_p.groupby("ncore").agg({"time": ["mean", "median", "min", plot_util.ci_lower, plot_util.ci_upper]})
        xs = df_p.index

        ys = t_min / df_p[("time", "mean")]
        ci_uppers = t_min / df_p[("time", "ci_lower")] - ys
        ci_lowers = ys - t_min / df_p[("time", "ci_upper")]

        xs_all.append(None)
        ys_all.append(None)
        ci_uppers_all.append(None)
        ci_lowers_all.append(None)

        xs_all.extend(xs)
        ys_all.extend(ys)
        ci_uppers_all.extend(ci_uppers)
        ci_lowers_all.extend(ci_lowers)

        error_y = dict(type="data", symmetric=False, array=ci_uppers_all, arrayminus=ci_lowers_all, thickness=linewidth)

        looks = policy_looks[policy]

        fig.add_trace(go.Scatter(
            x=xs_all,
            y=ys_all,
            error_y=error_y,
            line_width=linewidth,
            marker_line_width=linewidth,
            marker_color=looks["color"],
            marker_line_color=looks["color"],
            marker_symbol=looks["marker"],
            marker_size=markersize,
            line_dash=looks["dash"],
            name=looks["title"],
            legendrank=looks["rank"],
        ))

    fig.update_xaxes(
        showline=True,
        linecolor="black",
        ticks="outside",
        title_text="# of cores",
        title_standoff=5,
        mirror=True,
        **x_axis,
    )
    fig.update_yaxes(
        range=[0, 1],
        showline=True,
        linecolor="black",
        ticks="outside",
        title_text=yaxis_title,
        title_standoff=12,
        mirror=True,
        **y_axis,
    )
    fig.update_layout(
        width=400,
        height=330,
        margin=dict(l=0, r=0, b=0, t=0),
        legend=dict(
            yanchor="top",
            y=1.05,
            xanchor="left",
            x=0.02,
            itemwidth=itemwidth,
            bgcolor="rgba(0,0,0,0)",
        ),
        font=dict(
            family="Linux Biolinum O, sans-serif",
            size=16,
        ),
    )

    plot_util.save_fig(fig, fig_dir, "weak_{}.html".format(machine))
