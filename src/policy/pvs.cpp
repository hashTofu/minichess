#include <utility>
#include "state.hpp"
#include "pvs.hpp"


// PVS (negascout): first child gets full window, rest get null-window probe first
// re-search only happens when a move fails high (beats alpha unexpectedly)
int PVS::eval_ctx(
    State *state,
    int depth,
    int alpha,
    int beta,
    GameHistory& history,
    int ply,
    SearchContext& ctx,
    const PVSParams& p
){
    ctx.nodes++;
    if(ply > ctx.seldepth) ctx.seldepth = ply;
    if(ctx.stop) return 0;

    if(state->legal_actions.empty() && state->game_state == UNKNOWN)
        state->get_legal_actions();

    if(state->game_state == WIN) return P_MAX - ply;
    if(state->game_state == DRAW) return 0;

    int rep_score;
    if(state->check_repetition(history, rep_score)) return rep_score;
    history.push(state->hash());

    if(depth <= 0){
        history.pop(state->hash());
        if(p.use_quiescence)
            return quiesce(state, alpha, beta, history, ply, ctx, p);
        return state->evaluate(p.use_kp_eval, p.use_eval_mobility, &history);
    }

    bool first_child = true;
    for(auto& action : state->legal_actions){
        State* next = state->next_state(action);
        bool same = next->same_player_as_parent();

        int score;
        if(first_child){
            int raw = eval_ctx(next, depth - 1,
                same ? alpha : -beta,
                same ? beta  : -alpha,
                history, ply + 1, ctx, p);
            score = same ? raw : -raw;
            first_child = false;
        } else {
            // null window probe — just check if this move beats alpha at all
            int raw = eval_ctx(next, depth - 1,
                same ? alpha       : -(alpha + 1),
                same ? (alpha + 1) :  -alpha,
                history, ply + 1, ctx, p);
            score = same ? raw : -raw;

            // fail high: move is better than expected, get the real score
            if(!ctx.stop && score > alpha && score < beta){
                raw = eval_ctx(next, depth - 1,
                    same ? alpha : -beta,
                    same ? beta  : -alpha,
                    history, ply + 1, ctx, p);
                score = same ? raw : -raw;
            }
        }
        delete next;

        if(score > alpha) alpha = score;
        if(alpha >= beta){
            history.pop(state->hash());
            return alpha;
        }
    }

    history.pop(state->hash());
    return alpha;
}


// same idea as alphabeta's quiesce — captures only until the position is quiet
int PVS::quiesce(
    State *state,
    int alpha,
    int beta,
    GameHistory& history,
    int ply,
    SearchContext& ctx,
    const PVSParams& p
){
    ctx.nodes++;
    if(ply > ctx.seldepth) ctx.seldepth = ply;
    if(ctx.stop) return 0;

    if(state->legal_actions.empty() && state->game_state == UNKNOWN)
        state->get_legal_actions();

    if(state->game_state == WIN) return P_MAX - ply;
    if(state->game_state == DRAW) return 0;

    int rep_score;
    if(state->check_repetition(history, rep_score)) return rep_score;

    int stand_pat = state->evaluate(p.use_kp_eval, p.use_eval_mobility, &history);
    if(stand_pat >= beta) return stand_pat;
    if(stand_pat > alpha) alpha = stand_pat;

    history.push(state->hash());

    for(auto& action : state->legal_actions){
        int to_r = (int)action.second.first;
        int to_c = (int)action.second.second;
        if(state->piece_at(1 - state->player, to_r, to_c) == 0) continue; // skip quiet moves

        State* next = state->next_state(action);
        bool same = next->same_player_as_parent();

        int raw = quiesce(
            next,
            same ? alpha : -beta,
            same ? beta  : -alpha,
            history, ply + 1, ctx, p
        );
        int score = same ? raw : -raw;
        delete next;

        if(score > alpha) alpha = score;
        if(alpha >= beta) break;
    }

    history.pop(state->hash());
    return alpha;
}


SearchResult PVS::search(
    State *state,
    int depth,
    GameHistory& history,
    SearchContext& ctx
){
    ctx.reset();
    PVSParams p = PVSParams::from_map(ctx.params);
    SearchResult result;
    result.depth = depth;

    if(!state->legal_actions.size())
        state->get_legal_actions();

    int alpha = M_MAX;
    int beta  = P_MAX;
    int move_index = 0;
    int total_moves = (int)state->legal_actions.size();
    bool first_child = true;

    for(auto& action : state->legal_actions){
        State* next = state->next_state(action);
        bool same = next->same_player_as_parent();

        int score;
        if(first_child){
            int raw = eval_ctx(next, depth - 1,
                same ? alpha : -beta,
                same ? beta  : -alpha,
                history, 1, ctx, p);
            score = same ? raw : -raw;
            first_child = false;
        } else {
            int raw = eval_ctx(next, depth - 1,
                same ? alpha       : -(alpha + 1),
                same ? (alpha + 1) :  -alpha,
                history, 1, ctx, p);
            score = same ? raw : -raw;

            // beta = P_MAX at root so any fail-high triggers a re-search
            if(!ctx.stop && score > alpha){
                raw = eval_ctx(next, depth - 1,
                    same ? alpha : -beta,
                    same ? beta  : -alpha,
                    history, 1, ctx, p);
                score = same ? raw : -raw;
            }
        }
        delete next;

        if(score > alpha){
            alpha = score;
            result.best_move = action;
            if(p.report_partial && ctx.on_root_update)
                ctx.on_root_update({result.best_move, alpha, depth, move_index + 1, total_moves});
        }
        move_index++;
    }

    result.score = alpha;
    return result;
}


ParamMap PVS::default_params(){
    return {
        {"UseKPEval", "true"},
        {"UseEvalMobility", "true"},
        {"ReportPartial", "true"},
        {"UseQuiescence", "true"},
    };
}

std::vector<ParamDef> PVS::param_defs(){
    return {
        {"UseKPEval", ParamDef::CHECK, "true"},
        {"UseEvalMobility", ParamDef::CHECK, "true"},
        {"ReportPartial", ParamDef::CHECK, "true"},
        {"UseQuiescence", ParamDef::CHECK, "true"},
    };
}
