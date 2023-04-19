import os
import sys
import pathlib
import itertools
import math
import numpy as np
import pandas as pd
import plotly.subplots
import plotly.graph_objects as go
import plotly.io as pio

os.chdir(pathlib.Path(__file__).parent.parent.parent)
sys.path.append("./plot")
import plot_util

benchmark = "exafmm"

machine = "wisteria-o"

fig_dir = os.path.join("figs", benchmark)

pio.templates.default = "plotly_white"

linewidth = 2
itemwidth = 70
markersize = 12

n_warmups = 1

def get_result(batch_name, filename_template, **args):
    files = [(os.path.join(machine, benchmark, batch_name, filename_template.format(**dict(p))), dict(p))
             for p in itertools.product(*[[(k, v) for v in args[k]] for k in args])]
    df = plot_util.txt2df(files, [
            r'# of processes *: *(?P<nproc>\d+)',
            r'-* *Time average loop (?P<i>\d+) *-*',
        ], [
            r'Traverse *: *(?P<time>\d+\.\d+) *s',
        ])
    df = df.loc[df["i"] >= n_warmups].copy()
    return df

def get_parallel_result():
    return get_result("sbsize", "n_{n_input}_s_{sub_block_size}_{duplicate}.out",
                      n_input=[1_000_000, 10_000_000],
                      policy=["writeback_lazy"],
                      sub_block_size=[1, 4, 16, 64, 256, 1024, 4096, 16384, 65536],
                      duplicate=[0, 1, 2])

if __name__ == "__main__":
    fig = plotly.subplots.make_subplots(
        rows=2,
        cols=1,
        shared_xaxes=True,
        x_title="Sub-block size (bytes)",
        y_title="Execution time (s)",
        vertical_spacing=0.04,
    )

    log_axis = dict(type="log", dtick=1, minor=dict(ticks="inside", ticklen=5, showgrid=True))

    df = get_parallel_result()

    for i, (n_input, df_n) in enumerate(df.groupby("n_input")):
        df_n = df_n.groupby("sub_block_size").agg({"time": ["mean", "median", "min", plot_util.ci_lower, plot_util.ci_upper]})
        xs = df_n.index

        ys = df_n[("time", "mean")]
        ci_uppers = df_n[("time", "ci_upper")] - ys
        ci_lowers = ys - df_n[("time", "ci_lower")]

        error_y = dict(type="data", symmetric=False, array=ci_uppers, arrayminus=ci_lowers, thickness=linewidth)

        fig.add_trace(go.Scatter(
            x=xs,
            y=ys,
            error_y=error_y,
            line_width=linewidth,
            marker_line_width=linewidth,
            marker_size=markersize,
        ), row=2-i, col=1)

        y1 = ys.iloc[0]
        fig.add_annotation(
            x=0,
            y=y1,
            ax=8,
            ay=-25,
            text="{:.1f} s".format(y1),
            row=2-i, col=1,
        )

        y_min = ys.min()
        fig.add_annotation(
            x=math.log10(xs[ys.tolist().index(y_min)]),
            y=y_min,
            text="Best: {:.1f} s".format(y_min),
            row=2-i, col=1,
        )

    fig.add_annotation(
        x=0.04,
        y=0.4,
        xref="paper",
        yref="paper",
        showarrow=False,
        text="<b>1M bodies</b>",
    )
    fig.add_annotation(
        x=0.04,
        y=1.02,
        xref="paper",
        yref="paper",
        showarrow=False,
        text="<b>10M bodies</b>",
    )

    fig.update_xaxes(
        showline=True,
        linecolor="black",
        ticks="outside",
        **log_axis,
    )
    fig.update_yaxes(
        showline=True,
        linecolor="black",
        ticks="outside",
        rangemode="tozero",
    )
    fig.update_layout(
        width=350,
        height=250,
        margin=dict(l=60, r=15, b=50, t=10),
        showlegend=False,
        font=dict(
            family="Linux Biolinum O, sans-serif",
            size=16,
        ),
    )

    plot_util.save_fig(fig, fig_dir, "sbsize_{}.html".format(machine))
